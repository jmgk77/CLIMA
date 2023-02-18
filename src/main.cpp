#if !defined(ESP8266)
#error This code is designed to run on ESP8266 and ESP8266-based boards! Please check your Tools->Board setting.
#endif

#include <Arduino.h>

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266LLMNR.h>
#include <ESP8266NetBIOS.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <SSDP_esp8266.h>
#include <WiFiManager.h>

#define GETTIME_RETRIES 30

#define DHTPIN 2      // Pin which is connected to the DHT sensor.
#define DHTTYPE DHT11 // DHT 11

DHT_Unified dht(DHTPIN, DHTTYPE);

const char *device_name = "CLIMA";

WiFiManager wm;
ESP8266WebServer server;
ESP8266HTTPUpdateServer httpUpdater;

time_t boot_time, current_time;

float temperature = 0, humidity = 0;

#define MAX_TH_INFO 24 * 32

struct TH_INFO {
  float temperature;
  float humidity;
};

TH_INFO th_info[MAX_TH_INFO];

unsigned int th_index = 0;

const char html_header[] PROGMEM = R""""(<!DOCTYPE html>
<html lang='pt-br'>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<meta http-equiv='cache-control' content='no-cache, no-store, must-revalidate'>
<meta http-equiv='refresh' content='600; url=/' />
<link rel='stylesheet' href='https://cdn.simplecss.org/simple.min.css'>
<title>CLIMA</title>
</head>
<body><div>)"""";

const char html_footer[] PROGMEM = R""""(</div>
</body>
</html>
)"""";

void get_sensors() {
  // read sensors
  sensors_event_t event;

  temperature = 0;
  humidity = 0;

  dht.temperature().getEvent(&event);
  if (!isnan(event.temperature)) {
    temperature = (float)event.temperature;
  }
  dht.humidity().getEvent(&event);
  if (!isnan(event.relative_humidity)) {
    humidity = (float)event.relative_humidity;
  }
}

void send_html(const char *z) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send_P(200, "text/html", html_header);
  server.sendContent(z);
  server.sendContent(html_footer);
}

void handle_404() { send_html("<p>Not found!</p>"); }

#define GRAPH_RANGE 24 * 7

void handle_root() {
  // root
  char buf[512];
  get_sensors();
  snprintf_P(buf, sizeof(buf),
             PSTR("Temperature: %.01f<br>"
                  "Humidity: %.01f<br>"
                  "Data: %d<br>"),
             temperature, humidity, th_index);

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send_P(200, "text/html", html_header);
  server.sendContent(buf);

  ////////////////////////////
  int count = (th_index > GRAPH_RANGE) ? GRAPH_RANGE : th_index;
  int start = (th_index > GRAPH_RANGE) ? th_index - GRAPH_RANGE : 0;

  for (int i = 0; i < count; i++) {
    snprintf_P(buf, sizeof(buf), "%d -> %.01f,%.01f\n", i,
               th_info[start + i].temperature, th_info[start + i].humidity);
    server.sendContent(buf);
  }
  server.sendContent(html_footer);
}

void handle_raw() {
  // raw data
  char buf[512];
  get_sensors();
  snprintf(buf, sizeof(buf), "%f\n%f\n", temperature, humidity);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send_P(200, "text/plain", buf);
}

void handle_config() {
  // info
  char buf[2048];
  sensor_t sensor_t, sensor_h;

  dht.temperature().getSensor(&sensor_t);
  dht.humidity().getSensor(&sensor_h);

  FSInfo fs_info;
  SPIFFS.info(fs_info);

  snprintf_P(
      buf, sizeof(buf),
      PSTR("Build time: %s<br>Boot time: %s<br>IP: %s<br><br>"

           "ESP8266_INFO<br>ESP.getBootMode(): %d<br>ESP.getSdkVersion(): "
           "%s<br>ESP.getBootVersion(): %d<br>ESP.getChipId(): %08x<br>    "
           "ESP.getFlashChipSize(): %d<br>ESP.getFlashChipRealSize(): "
           "%d<br>ESP.getFlashChipSizeByChipId(): %d<br>ESP.getFlashChipId(): "
           "%08x<br>ESP.getFreeHeap(): %d<br><br>"

           "DHT11_TEMP<br>t.sensor(): %s<br>t.driver(): %d<br>t.id(): "
           "%d<br>t.max(): %f<br>t.min(): %f<br>t.resolution(): %f<br><br>"

           "DHT11_HUMIDITY<br>h.sensor(): %s<br>h.driver(): %d<br>h.id(): "
           "%d<br>h.max(): %f<br>h.min(): %f<br>h.resolution(): %f<br><br>"

           "FS_INFO<br>fs_info.totalBytes(): %d<br>fs_info.usedBytes(): "
           "%d<br>fs_info.blockSize(): %d<br>fs_info.pageSize(): "
           "%d<br>fs_info.maxOpenFiles(): %d<br>fs_info.maxPathLength(): "
           "%d<br><br>"

           "<a href='update'><button>update</button></a><br><a "
           "href='reboot'><button>reboot</button></a><br><a "
           "href='reset'><button>reset</button></a>"),
      __DATE__ " " __TIME__, ctime(&boot_time),
      WiFi.localIP().toString().c_str(), ESP.getBootMode(), ESP.getSdkVersion(),
      ESP.getBootVersion(), ESP.getChipId(), ESP.getFlashChipSize(),
      ESP.getFlashChipRealSize(), ESP.getFlashChipSizeByChipId(),
      ESP.getFlashChipId(), ESP.getFreeHeap(), sensor_t.name, sensor_t.version,
      sensor_t.sensor_id, sensor_t.max_value, sensor_t.min_value,
      sensor_t.resolution, sensor_h.name, sensor_h.version, sensor_h.sensor_id,
      sensor_h.max_value, sensor_h.min_value, sensor_h.resolution,
      fs_info.totalBytes, fs_info.usedBytes, fs_info.blockSize,
      fs_info.pageSize, fs_info.maxOpenFiles, fs_info.maxPathLength);

  send_html(buf);
}

void handle_reboot() {
  // reboot ESP01
  server.send(200, "text/html",
              "<meta http-equiv='refresh' content='30; url=/' />");
  delay(1 * 1000);
  ESP.restart();
  delay(2 * 1000);
}

void handle_reset() {
  // reset wifi credentials
  wm.resetSettings();
  handle_reboot();
}

void handle_files() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  char buf[512];
  // download
  if (server.hasArg("n")) {
    String fname = server.arg("n");
    File f = SPIFFS.open(fname, "r");
    server.setContentLength(f.size());
    server.send(200, "application/octet-stream", "");
    int r;
    do {
      r = f.read((uint8_t *)&buf, sizeof(buf));
      server.sendContent(buf, r);
    } while (r == sizeof(buf));
    f.close();
  } else if (server.hasArg("x"))
  // delete
  {
    String fname = server.arg("x");
    SPIFFS.remove(fname);
    server.send(200, "text/html",
                "<script>document.location.href = '/files'</script>");
  } else
  // dir list
  {
    String s;
    server.send_P(200, "text/html", html_header);

    //    Dir dir = SPIFFS.openDir("/");
    Dir dir = SPIFFS.openDir("");

    while (dir.next()) {
      if (dir.isFile()) {
        s = "<a download='" + dir.fileName() +
            "' href='files?n=" + dir.fileName() + "'><button>" +
            dir.fileName() + "</button></a>";
        itoa(dir.fileSize(), buf, 10);
        s += "    (" + String(buf) + ")    ";
        const time_t t = dir.fileTime();
        s += String(ctime(&t));
        s += "<a href='files?x=" + dir.fileName() +
             "'><button>x</button></a><br>";
        server.sendContent(s);
      }
    }
    server.sendContent("<br><br><a href='/'><button>BACK</button></a><br>");
    server.sendContent(html_footer);
  }
}

void setup() {
  // setup WIFI
  WiFi.mode(WIFI_STA);
  delay(10);
  wm.setDebugOutput(false);
  WiFi.hostname(device_name);
  wm.setConfigPortalTimeout(180);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  // connect to wifi
  if (!wm.autoConnect(device_name)) {
    ESP.restart();
    delay(1 * 1000);
  }

  // install www handlers
  httpUpdater.setup(&server, "/update");
  server.onNotFound(handle_404);
  server.on("/", handle_root);
  server.on("/raw", handle_raw);
  server.on("/config", handle_config);
  server.on("/reboot", handle_reboot);
  server.on("/reset", handle_reset);
  server.on("/files", handle_files);
  server.on("/description.xml", HTTP_GET,
            []() { SSDP_esp8266.schema(server.client()); });
  server.begin();

  // discovery protocols
  MDNS.begin(device_name);
  MDNS.addService("http", "tcp", 80);
  NBNS.begin(device_name);
  LLMNR.begin(device_name);
  SSDP_esp8266.setName(device_name);
  SSDP_esp8266.setDeviceType("urn:schemas-upnp-org:device:CLIMA:1");
  SSDP_esp8266.setSchemaURL("description.xml");
  SSDP_esp8266.setSerialNumber(ESP.getChipId());
  SSDP_esp8266.setURL("/");
  SSDP_esp8266.setModelName(device_name);
  SSDP_esp8266.setModelNumber("1");
  SSDP_esp8266.setManufacturer("JMGK");
  SSDP_esp8266.setManufacturerURL("http://www.jmgk.com.br/");

  // get internet time
  configTime("<-03>3", "pool.ntp.org");
  // verifica 2021...
  int t = 0;
  while ((time(nullptr) < 1609459200) && (t < GETTIME_RETRIES)) {
    t++;
    delay(100);
  }

  // set boot/current time
  current_time = boot_time = time(NULL);

  // init sensor
  dht.begin();

  // init filesystem
  SPIFFS.begin();

  // load temporary binary cache
  File f = SPIFFS.open("CACHE", "r");
  if (f) {
    th_index = f.read((uint8_t *)&th_info, sizeof(th_info));
    th_index /= sizeof(TH_INFO);
    f.close();
  }
}

void loop() {
  // web things
  server.handleClient();
  MDNS.update();
  SSDP_esp8266.handleClient();

  // log temperatura and humidity
  struct tm now, last;
  time_t t = time(NULL);
  gmtime_r(&t, &now);
  gmtime_r(&current_time, &last);
  // check if hour changed
  if (now.tm_hour != last.tm_hour) {
    current_time = t;
    get_sensors();
    // save hourly data
    th_info[th_index].temperature = temperature;
    th_info[th_index].humidity = humidity;
    th_index++;

    // write temporary binary cache
    File f = SPIFFS.open("CACHE", "w");
    if (f) {
      f.write((uint8_t *)&th_info, th_index * sizeof(TH_INFO));
      f.close();
    }

    //  check if day changed
    if (now.tm_mday != last.tm_mday) {
      // gera nome do arquivo
      char buf[64];
      snprintf(buf, sizeof(buf), "%02d_%02d.%04d", now.tm_mday, now.tm_mon,
               now.tm_year + 1900);
      // write arquivo diario
      File f = SPIFFS.open(buf, "w");
      if (f) {
        // if we have more than 24 entries, write last 24
        for (unsigned int i = (th_index < 24) ? 0 : (th_index - 24);
             i < th_index; i++) {
          // write
          f.printf("%.01f,%.01f\n", th_info[i].temperature,
                   th_info[i].humidity);
        }
        // close arquivo diario
        f.close();
      }
    }

    // check if month changed
    if (now.tm_mon != last.tm_mon) {
      // gera nome do arquivo
      char buf[64];
      snprintf(buf, sizeof(buf), "%02d.%04d", now.tm_mon, now.tm_year + 1900);
      // write arquivo mensal
      File f = SPIFFS.open(buf, "w");
      if (f) {
        // write whole data
        for (unsigned int i = 0; i < th_index; i++) {
          // write
          f.printf("%.01f,%.01f\n", th_info[i].temperature,
                   th_info[i].humidity);
        }
        // close arquivo mensal
        f.close();
      }

      // reset data
      th_index = 0;
      SPIFFS.remove("CACHE");
    }
  }
}
