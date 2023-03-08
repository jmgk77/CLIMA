// CLIMA
//
// Firmware for https://aliexpress.com/item/32840839415.html
//
// * show current temp/humidty
// * show current month temp/humidty graph
// * save daily/monthly info in CSV format
// * partial data survive reboots
// * file server/update server
// * WiFi wizard
// * SSDP, LLMR, MDNS. NBNS discovery protocols
// * MQTT support

// ??? show monthly history (read from disk)

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
#include <ESP_EEPROM.h>
#include <FS.h>
#include <PubSubClient.h>
#include <SSDP_esp8266.h>
#include <WiFiManager.h>

#include "version.h"

#define DAILY_FILE

const char *device_name = "CLIMA";

// time
#define GETTIME_RETRIES 30
bool notime;
time_t boot_time, current_time;

// sensor
#define DHTPIN 2      // Pin which is connected to the DHT sensor.
#define DHTTYPE DHT11 // DHT 11
DHT_Unified dht(DHTPIN, DHTTYPE);
float temperature = 0, humidity = 0;

// mqtt
#define MQTT_REFRESH 1
#define MQTT_CLIMA_LOCALIP "CLIMA/IP"
#define MQTT_CLIMA_TEMPERATURE "CLIMA/TEMPERATURE"
#define MQTT_CLIMA_HUMIDITY "CLIMA/HUMIDITY"
unsigned long mqtt_interval;
WiFiClient mqtt_client;
PubSubClient mqtt(mqtt_client);

// eeprom
#define EEPROM_SIGNATURE 'J'
struct eeprom_data {
  char sign = EEPROM_SIGNATURE;
  char mqtt_server[32]; // = "192.168.0.250";
  unsigned int mqtt_server_port = 1883;
} eeprom;

// www
WiFiManager wm;
ESP8266WebServer server;
ESP8266HTTPUpdateServer httpUpdater;

// graph
#define GRAPH_RANGE 24 * 7
#define MAX_TH_INFO 24 * 32
struct TH_INFO {
  time_t tempo;
  float temperature;
  float humidity;
};
TH_INFO th_info[MAX_TH_INFO];
unsigned int th_index = 0;

// html
const char html_header[] PROGMEM = R""""(<!DOCTYPE html>
<html lang='pt-br'>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<meta http-equiv='cache-control' content='no-cache, no-store, must-revalidate'>
<meta http-equiv='refresh' content='600; url=/'>
<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
<link rel='stylesheet' href='https://cdn.simplecss.org/simple.min.css'>
<title>CLIMA</title>
</head>
<body><div style='text-align: center'>
<a href='/'><button>MAIN</button></a>
<a href='config'><button>CONFIG</button></a>
<a href='files'><button>FILES</button></a>
)"""";

const char html_footer[] PROGMEM = R""""(
</div>
</body>
</html>
)"""";

const char html_config[] PROGMEM = R""""(
<div style='border: 1px solid black'>
Version: %s<br>
Boot time: %s<br>
Last reading: %s<br>
IP: %s<br>
ESP.getSketchSize(): %d<br>
ESP.getFreeSketchSpace(): %d<br>
fs_info.totalBytes(): %d<br>
fs_info.usedBytes(): %d<br>
</div>
<div style='border: 1px solid black'>
<form action='/config' method='POST'>
<label for='mqtt_server'>MQTT Broker IP:</label>
<input type='text' name='mqtt_server' value='%s'><br>
<label for='mqtt_server_port'>MQTT Broker Port:</label>
<input type='text' name='mqtt_server_port' value='%d'><br>
<input type='hidden' name='s' value='1'>
<input type='submit' value='SAVE'></form>
</div>
<a href='update'><button>UPDATE</button></a>
<a href='reboot'><button>REBOOT</button></a>
<a href='reset'><button>RESET</button></a>
)"""";

const char html_javascript[] PROGMEM = R""""(];
var canvas = document.getElementById('c');
var ctx = canvas.getContext('2d');
var myChart = new Chart(ctx, {
  type: 'line',
  data: {
    labels: l,
    datasets: [{
      label: 'Temperature',
      data: t,
      borderColor: 'rgb(255, 0, 0)',
      backgroundColor: 'rgb(255, 0, 0, 0.1)',
      tension: 0.1,
    }, {
      label: 'Humidity',
      data: h,
      borderColor: 'rgb(0, 0, 255)',
      backgroundColor: 'rgb(0, 0, 255, 0.1)',
      tension: 0.1,}]
    },
  }
);
</script>
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

void handle_root() {
  // root
  char buf[512];
  get_sensors();
  snprintf_P(buf, sizeof(buf),
             PSTR("<div style='border: 1px solid black'>Temperature: %.01f<br>"
                  "Humidity: %.01f<br>"
                  "<canvas id='c' width='600' height='200'></canvas></div>"),
             temperature, humidity, th_index);

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send_P(200, "text/html", html_header);
  server.sendContent(buf);

  // calcula quantos itens vamos mostrar
  int count = (th_index > GRAPH_RANGE) ? GRAPH_RANGE : th_index;
  int start = (th_index > GRAPH_RANGE) ? th_index - GRAPH_RANGE : 0;

  // write javascript variables
  server.sendContent("<script>const t = [");
  for (int i = 0; i < count; i++) {
    snprintf_P(buf, sizeof(buf), "%.01f,", th_info[start + i].temperature);
    server.sendContent(buf);
  }
  server.sendContent("];\nconst h = [");
  for (int i = 0; i < count; i++) {
    snprintf_P(buf, sizeof(buf), "%.01f,", th_info[start + i].humidity);
    server.sendContent(buf);
  }
  server.sendContent("];\nconst l = [");
  for (int i = 0; i < count; i++) {
    strftime(buf, sizeof(buf), "\"%c\",", localtime(&th_info[start + i].tempo));
    server.sendContent(buf);
  }

  // write javascript
  server.sendContent_P(html_javascript);
  server.sendContent(html_footer);
}

void handle_raw() {
  // raw data
  char buf[512];
  get_sensors();
  snprintf(buf, sizeof(buf), "%.2f\n%.2f\n", temperature, humidity);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send_P(200, "text/plain", buf);
}

void handle_config() {
  if (server.hasArg("s")) {
    strncpy(eeprom.mqtt_server, server.arg("mqtt_server").c_str(),
            sizeof(eeprom.mqtt_server));
    eeprom.mqtt_server_port = server.arg("mqtt_server_port").toInt();
    EEPROM.put(0, eeprom);
    EEPROM.commit();
    server.send(200, "text/html",
                "<meta http-equiv='refresh' content='0; url=/config' />");
  } else {
    // info
    char buf[1536];

    FSInfo fs_info;
    SPIFFS.info(fs_info);

    char t1[32], t2[32];
    ctime_r(&boot_time, t1);
    ctime_r(&current_time, t2);

    snprintf_P(buf, sizeof(buf), html_config, VERSION, t1,
               notime ? "<font color='red'>NOTIME</font>" : t2,
               WiFi.localIP().toString().c_str(), ESP.getSketchSize(),
               ESP.getFreeSketchSpace(), fs_info.totalBytes, fs_info.usedBytes,
               eeprom.mqtt_server, eeprom.mqtt_server_port);
    send_html(buf);
  }
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
  // erase eeprom
  eeprom = {};
  EEPROM.put(0, eeprom);
  EEPROM.commit();
  // reset wifi credentials
  wm.resetSettings();
  handle_reboot();
}

void handle_files() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  char buf[512];
  if (server.hasArg("n")) {
    // download
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
  } else if (server.hasArg("x")) {
    // delete
    String fname = server.arg("x");
    SPIFFS.remove(fname);
    server.send(200, "text/html",
                "<script>document.location.href = '/files'</script>");
  } else {
    // dir
    String s;
    server.send_P(200, "text/html", html_header);
    server.sendContent("<div style='border: 1px solid black'>");

    // scan files
    Dir dir = SPIFFS.openDir("");
    while (dir.next()) {
      if (dir.isFile()) {
        s = "<a download='" + dir.fileName() +
            "' href='files?n=" + dir.fileName() + "'>" + dir.fileName() +
            "</a>";
        itoa(dir.fileSize(), buf, 10);
        s += "    (" + String(buf) + ")    ";
        const time_t t = dir.fileTime();
        s += String(ctime(&t));
        s += "<a href='files?x=" + dir.fileName() + "'>x</a><br>";
        server.sendContent(s);
      }
    }

    server.sendContent("</div>");
    server.sendContent(html_footer);
  }
}

void get_time() {
  // get internet time
  configTime("<-03>3", "pool.ntp.org");
  // verify 2021...
  int t = 0;
  while ((time(nullptr) < 1609459200) && (t < GETTIME_RETRIES)) {
    t++;
    delay(100);
  }
  notime = (time(nullptr) < 1609459200) ? true : false;
}

void setup() {
  EEPROM.begin(sizeof(eeprom_data));

  // if there's valid EEPROM config, load it
  EEPROM.get(0, eeprom);
  if (eeprom.sign != EEPROM_SIGNATURE) {
    // default eeprom
    eeprom = {};
  }

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

  // mqtt setup
  if (strlen(eeprom.mqtt_server)) {
    mqtt.setServer(eeprom.mqtt_server, eeprom.mqtt_server_port);
    mqtt.setCallback([](char *, byte *, unsigned int) {});
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

  // set boot/current time
  get_time();
  current_time = boot_time = time(NULL);

  // init sensor
  dht.begin();

  // init filesystem
  SPIFFS.begin();

  // load temporary binary cache
  File f = SPIFFS.open("/CACHE", "r");
  if (f) {
    th_index = f.read((uint8_t *)&th_info, sizeof(th_info));
    th_index /= sizeof(TH_INFO);
    f.close();
    // get last read time from cache
    if (th_index) {
      current_time = th_info[th_index - 1].tempo;
    }
  }
}

void dump_csv(char *nome, unsigned int inicio) {
  char buf[64];

  // create
  File f = SPIFFS.open(nome, "w");
  if (f) {
    // CSV header
    f.printf("Hora, Data, Temperatura, Humidade\n");
    // loop database
    for (unsigned int i = inicio; i < (th_index - 1); i++) {
      // write
      strftime(buf, sizeof(buf), "%T, %d-%m-%Y", localtime(&th_info[i].tempo));
      f.printf("%s, %.01f, %.01f\n", buf, th_info[i].temperature,
               th_info[i].humidity);
    }
    // close
    f.close();
  }
}

void loop() {
  char buf[64];

  // web things
  server.handleClient();
  MDNS.update();
  SSDP_esp8266.handleClient();

  if ((strlen(eeprom.mqtt_server)) &&
      (!mqtt.connected() ||
       ((millis() - mqtt_interval) >= (MQTT_REFRESH * 60 * 1000UL)))) {
    mqtt_interval = millis();
    mqtt.connect("CLIMA");
    mqtt.publish(MQTT_CLIMA_LOCALIP, WiFi.localIP().toString().c_str());
    snprintf(buf, sizeof(buf), "%.2f", temperature);
    mqtt.publish(MQTT_CLIMA_TEMPERATURE, buf);
    snprintf(buf, sizeof(buf), "%.2f", humidity);
    mqtt.publish(MQTT_CLIMA_HUMIDITY, buf);
  }
  mqtt.loop();

  // we cant do anything till we get the clock
  if (notime) {
    get_time();
  } else {
    struct tm now, last, yesterday;
    // get date/time now
    time_t t = time(NULL);
    localtime_r(&t, &now);
    // get last update date
    localtime_r(&current_time, &last);
    // get yesterday date (for filenames porpouse)
    yesterday = now;
    yesterday.tm_mday--;
    mktime(&yesterday);

    // check if hour changed
    if (now.tm_hour != last.tm_hour) {
      current_time = t;
      get_sensors();

      // log temperatura and humidity
      th_info[th_index].tempo = t;
      th_info[th_index].temperature = temperature;
      th_info[th_index].humidity = humidity;
      th_index++;

      // write temporary binary cache
      File f = SPIFFS.open("/CACHE", "w");
      if (f) {
        f.write((uint8_t *)&th_info, th_index * sizeof(TH_INFO));
        f.close();
      }

#ifdef DAILY_FILE
      //  check if day changed
      if (now.tm_mday != last.tm_mday) {
        // gera nome do arquivo
        strftime(buf, sizeof(buf), "/%d%m%Y.csv", &yesterday);
        // write arquivo diario
        dump_csv(buf, (th_index < 24) ? 0 : (th_index - 25));
      }
#endif

      // check if month changed
      if (now.tm_mon != last.tm_mon) {
        // gera nome do arquivo
        strftime(buf, sizeof(buf), "/%m%Y.csv", &yesterday);
        // write arquivo mensal
        dump_csv(buf, 0);

        // reset data (move last entry to first)
        th_info[0].tempo = th_info[th_index - 1].tempo;
        th_info[0].temperature = th_info[th_index - 1].temperature;
        th_info[0].humidity = th_info[th_index - 1].humidity;

        th_index = 1;
        SPIFFS.remove("/CACHE");
      }
    }
  }
}