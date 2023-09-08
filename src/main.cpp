/*
 ██████╗██╗     ██╗███╗   ███╗ █████╗
██╔════╝██║     ██║████╗ ████║██╔══██╗
██║     ██║     ██║██╔████╔██║███████║
██║     ██║     ██║██║╚██╔╝██║██╔══██║
╚██████╗███████╗██║██║ ╚═╝ ██║██║  ██║
 ╚═════╝╚══════╝╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
*/

/*
v0:
* show current temp/humidty
* show current month temp/humidty graph
* save daily/monthly info in CSV format
* partial data survive reboots
* file server/update server
* WiFi wizard
* SSDP, LLMR, MDNS. NBNS discovery protocols
* MQTT support

v1:
* SHT30 support
* WeMos D1

v1.1:
* BME280 support

TODO:
* show monthly history (read from disk)
*/

#if !defined(ESP8266)
#error This code is designed to run on ESP8266 and ESP8266-based boards! Please check your Tools->Board setting.
#endif

#define DEBUG

// #define DAILY_FILE

#define SENSOR_BME280

const char *device_name = "CLIMA";

#include <Arduino.h>

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

#ifdef SENSOR_BME280
#include <Adafruit_BME280.h>
#include <Adafruit_Sensor.h>
#else
#include <WEMOS_SHT3X.h>
#endif

#include "version.h"

// time
#define GETTIME_RETRIES 30
bool notime;
time_t boot_time, current_time;

// sensor
#ifdef SENSOR_BME280
// Adafruit_BME280 bme;
// assign the ESP8266 pins to arduino pins
#define D1 5
#define D2 4
#define D4 2
#define D3 0

// assign the SPI bus to pins
#define BME_SCK D1
#define BME_MISO D4
#define BME_MOSI D2
#define BME_CS D3

#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME280 bme(BME_CS, BME_MOSI, BME_MISO, BME_SCK); // software SPI
#else
SHT3X sht30(0x44);
#endif
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
  bool mqtt_enabled;
  char mqtt_server[128]; // = "192.168.0.250";
  unsigned int mqtt_server_port = 1883;
  char mqtt_username[32];
  char mqtt_password[32];
} eeprom;

// www
WiFiManager wm;
ESP8266WebServer server;
ESP8266HTTPUpdateServer httpUpdater;

#define ENABLE_WWW_UPLOAD

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

/*
██╗  ██╗████████╗███╗   ███╗██╗
██║  ██║╚══██╔══╝████╗ ████║██║
███████║   ██║   ██╔████╔██║██║
██╔══██║   ██║   ██║╚██╔╝██║██║
██║  ██║   ██║   ██║ ╚═╝ ██║███████╗
╚═╝  ╚═╝   ╚═╝   ╚═╝     ╚═╝╚══════╝
*/

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
)"""";

const char html_config2[] PROGMEM = R""""(
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
canvas = document.getElementById('a');
ctx = canvas.getContext('2d');
myChart = new Chart(ctx, {
  type: 'line',
  data: {
    labels: l.slice(-24),
    datasets: [{
      label: 'Temperature',
      data: t.slice(-24),
      borderColor: 'rgb(255, 0, 0)',
      backgroundColor: 'rgb(255, 0, 0, 0.1)',
      tension: 0.1,
    }]
  },
});
canvas = document.getElementById('b');
ctx = canvas.getContext('2d');
myChart = new Chart(ctx, {
  type: 'line',
  data: {
    labels: l.slice(-24),
    datasets: [{
      label: 'Humidity',
      data: h.slice(-24),
      borderColor: 'rgb(0, 0, 255)',
      backgroundColor: 'rgb(0, 0, 255, 0.1)',
      tension: 0.1,
    }]
  },
});
</script>
)"""";

/*
███████╗███████╗███╗   ██╗███████╗ ██████╗ ██████╗
██╔════╝██╔════╝████╗  ██║██╔════╝██╔═══██╗██╔══██╗
███████╗█████╗  ██╔██╗ ██║███████╗██║   ██║██████╔╝
╚════██║██╔══╝  ██║╚██╗██║╚════██║██║   ██║██╔══██╗
███████║███████╗██║ ╚████║███████║╚██████╔╝██║  ██║
╚══════╝╚══════╝╚═╝  ╚═══╝╚══════╝ ╚═════╝ ╚═╝  ╚═╝
*/

void get_sensors() {
  // read sensors

  temperature = 0;
  humidity = 0;

#ifdef SENSOR_BME280
  temperature = bme.readTemperature();
  humidity = bme.readHumidity();
#ifdef DEBUG
  Serial.print(bme.readPressure() / 100.0F);
  Serial.println(" hPa");
#endif
#else
  sht30.get();
  temperature = sht30.cTemp;
  humidity = sht30.humidity;
#endif
#ifdef DEBUG
  Serial.println("SENSOR");
#endif
}

/*
██╗    ██╗███████╗██████╗
██║    ██║██╔════╝██╔══██╗
██║ █╗ ██║█████╗  ██████╔╝
██║███╗██║██╔══╝  ██╔══██╗
╚███╔███╔╝███████╗██████╔╝
 ╚══╝╚══╝ ╚══════╝╚═════╝
*/

void send_html(const char *z) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send_P(200, "text/html", html_header);
  server.sendContent(z);
  server.sendContent(html_footer);
}

void handle_404() {
#ifdef DEBUG
  Serial.println("WWW 404");
#endif
  send_html("<p>Not found!</p>");
}

void handle_root() {
// root
#ifdef DEBUG
  Serial.println("WWW ROOT");
#endif

  char buf[512];
  get_sensors();
  snprintf_P(buf, sizeof(buf),
             PSTR("<div style='border: 1px solid black'>Temperature: %.01f<br>"
                  "Humidity: %.01f<br>"
                  "<br><canvas id='a' width='600' height='200'></canvas>"
                  "<br><canvas id='b' width='600' height='200'></canvas>"
                  "<br><canvas id='c' width='600' height='200'></canvas>"
                  "</div>"),
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
#ifdef DEBUG
  Serial.println("WWW RAW");
#endif

  char buf[512];
  get_sensors();
  snprintf(buf, sizeof(buf), "%.2f\n%.2f\n", temperature, humidity);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send_P(200, "text/plain", buf);
}

#define FORM_SAVE_STRING(VAR)                                                  \
  strncpy(eeprom.VAR, server.arg(#VAR).c_str(), sizeof(eeprom.VAR));
#define FORM_SAVE_INT(VAR) eeprom.VAR = server.arg(#VAR).toInt();
#define FORM_SAVE_BOOL(VAR)                                                    \
  eeprom.VAR = server.arg(#VAR) == "on" ? true : false;

#define FORM_START(URL)                                                        \
  s += "<form action='" + String(URL) + "' method='POST'>";
#define FORM_ASK_VALUE(VAR, TXT)                                               \
  s += "<label for='" + String(#VAR) + "'>" + String(TXT) +                    \
       ":</label><input type='text' name='" + String(#VAR) + "' value='" +     \
       eeprom.VAR + "'><br>";
#define FORM_ASK_BOOL(VAR, TXT)                                                \
  s += "<label for='" + String(#VAR) + "'>" + String(TXT) +                    \
       ":</label><input type='checkbox' name='" + String(#VAR) + "' " +        \
       String(eeprom.VAR ? "checked" : "") + "><br>";
#define FORM_END(BTN)                                                          \
  s +=                                                                         \
      "<input type='hidden' name='s' value='1'><input type='submit' value='" + \
      String(BTN) + "'></form><br>";

void handle_config() {
  if (server.hasArg("s")) {
#ifdef DEBUG
    Serial.println("WWW CONFIG SAVE");
#endif
    FORM_SAVE_BOOL(mqtt_enabled);
    FORM_SAVE_STRING(mqtt_server);
    FORM_SAVE_INT(mqtt_server_port);
    FORM_SAVE_STRING(mqtt_username);
    FORM_SAVE_STRING(mqtt_password);
    EEPROM.put(0, eeprom);
    EEPROM.commit();
    server.send(200, "text/html",
                "<meta http-equiv='refresh' content='0; url=/config' />");
  } else {
// info
#ifdef DEBUG
    Serial.println("WWW CONFIG");
#endif
    char buf[1536];

    FSInfo fs_info;
    SPIFFS.info(fs_info);

    char t1[32], t2[32];
    ctime_r(&boot_time, t1);
    ctime_r(&current_time, t2);

    snprintf_P(buf, sizeof(buf), html_config, VERSION, t1,
               notime ? "<font color='red'>NOTIME</font>" : t2,
               WiFi.localIP().toString().c_str(), ESP.getSketchSize(),
               ESP.getFreeSketchSpace(), fs_info.totalBytes, fs_info.usedBytes);

    String s = buf;
    FORM_START("/config");
    FORM_ASK_BOOL(mqtt_enabled, "MQTT");
    FORM_ASK_VALUE(mqtt_server, "MQTT Broker IP");
    FORM_ASK_VALUE(mqtt_server_port, "MQTT Broker Port");
    FORM_ASK_VALUE(mqtt_username, "MQTT Username");
    FORM_ASK_VALUE(mqtt_password, "MQTT Password");
    FORM_END("Salvar");
    s += html_config2;

    send_html(s.c_str());
  }
}

void handle_reboot() {
// reboot ESP8266
#ifdef DEBUG
  Serial.println("WWW REBOOT");
#endif

  server.send(200, "text/html",
              "<meta http-equiv='refresh' content='30; url=/' />");
  delay(1 * 1000);
  ESP.restart();
  delay(2 * 1000);
}

void handle_reset() {
// erase eeprom
#ifdef DEBUG
  Serial.println("WWW RESET");
#endif

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
#ifdef DEBUG
    Serial.println("WWW FILE DOWNLOAD");
#endif
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
#ifdef DEBUG
    Serial.println("WWW FILE DELETE");
#endif
    // delete
    String fname = server.arg("x");
    SPIFFS.remove(fname);
    server.send(200, "text/html",
                "<script>document.location.href = '/files'</script>");
  } else {
// dir
#ifdef DEBUG
    Serial.println("WWW FILE");
#endif

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
#ifdef ENABLE_WWW_UPLOAD
    server.sendContent(
        "<form action='/upload' method='POST' "
        "enctype='multipart/form-data'><input type='file' name='name'><input "
        "class='button' type='submit' value='Upload'></form>");
#endif
    server.sendContent("</div>");
    server.sendContent(html_footer);
  }
}

#ifdef ENABLE_WWW_UPLOAD
File fsUploadFile;

void handle_upload() {
#ifdef DEBUG
  Serial.println("WWW UPLOAD");
#endif
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    fsUploadFile = SPIFFS.open("/" + upload.filename, "w");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    fsUploadFile.close();
  }
}
#endif

/*
███╗   ███╗██╗███████╗ ██████╗
████╗ ████║██║██╔════╝██╔════╝
██╔████╔██║██║███████╗██║
██║╚██╔╝██║██║╚════██║██║
██║ ╚═╝ ██║██║███████║╚██████╗
╚═╝     ╚═╝╚═╝╚══════╝ ╚═════╝
*/

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

void dump_csv(char *nome, unsigned int inicio) {
  char buf[64];

  // create
  File f = SPIFFS.open(nome, "w");
  if (f) {
    // CSV header
    f.printf("Hora, Data, Temperatura, Umidade\n");
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

/*
███████╗███████╗████████╗██╗   ██╗██████╗
██╔════╝██╔════╝╚══██╔══╝██║   ██║██╔══██╗
███████╗█████╗     ██║   ██║   ██║██████╔╝
╚════██║██╔══╝     ██║   ██║   ██║██╔═══╝
███████║███████╗   ██║   ╚██████╔╝██║
╚══════╝╚══════╝   ╚═╝    ╚═════╝ ╚═╝
*/

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  Serial.println("SETUP");
#endif

  EEPROM.begin(sizeof(eeprom_data));

  // if there's valid EEPROM config, load it
  EEPROM.get(0, eeprom);
  if (eeprom.sign != EEPROM_SIGNATURE) {
    // default eeprom
    eeprom = {};
  }
#ifdef DEBUG
  Serial.println("EEPROM");
#endif

  // setup WIFI
  WiFi.mode(WIFI_STA);
  delay(10);
  wm.setDebugOutput(false);
  WiFi.hostname(device_name);
  wm.setConfigPortalTimeout(180);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
#ifdef DEBUG
  Serial.println("WIFI");
#endif

  // connect to wifi
  if (!wm.autoConnect(device_name)) {
    ESP.restart();
    delay(1 * 1000);
  }
#ifdef DEBUG
  Serial.println("CONNECTED " + WiFi.localIP().toString());
#endif

  // mqtt setup
  if (eeprom.mqtt_enabled) {
    mqtt.setServer(eeprom.mqtt_server, eeprom.mqtt_server_port);
    mqtt.setCallback([](char *, byte *, unsigned int) {});
#ifdef DEBUG
    Serial.println("MQTT");
#endif
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
#ifdef ENABLE_WWW_UPLOAD
  server.on(
      "/upload", HTTP_POST,
      []() {
        server.send(200, "text/html",
                    "<meta http-equiv='refresh' content='0; url=/files' />");
      },
      handle_upload);
#endif
  server.begin();
#ifdef DEBUG
  Serial.println("WWW");
#endif

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
#ifdef DEBUG
  Serial.println("DISCOVER");
#endif

  // set boot/current time
  get_time();
  current_time = boot_time = time(NULL);
#ifdef DEBUG
  Serial.println("TIME");
#endif

#ifdef SENSOR_BME280
  unsigned status = bme.begin();
  // You can also pass in a Wire library object like &Wire2
  // status = bme.begin(0x76, &Wire2)
  if (!status) {
    Serial.println("Could not find a valid BME280 sensor, check wiring, "
                   "address, sensor ID!");
    Serial.print("SensorID was: 0x");
    Serial.println(bme.sensorID(), 16);
    Serial.print("        ID of 0xFF probably means a bad address, a BMP 180 "
                 "or BMP 085\n");
    Serial.print("   ID of 0x56-0x58 represents a BMP 280,\n");
    Serial.print("        ID of 0x60 represents a BME 280.\n");
    Serial.print("        ID of 0x61 represents a BME 680.\n");
    while (1)
      delay(10);
  }
#endif

  // init filesystem
  SPIFFS.begin();
#ifdef DEBUG
  Serial.println("FS");
#endif

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
#ifdef DEBUG
  Serial.println("CACHE");
#endif

  // setup end
}

/*
██╗      ██████╗  ██████╗ ██████╗
██║     ██╔═══██╗██╔═══██╗██╔══██╗
██║     ██║   ██║██║   ██║██████╔╝
██║     ██║   ██║██║   ██║██╔═══╝
███████╗╚██████╔╝╚██████╔╝██║
╚══════╝ ╚═════╝  ╚═════╝ ╚═╝
*/

void loop() {
  char buf[64];

  // web things
  server.handleClient();
  MDNS.update();
  SSDP_esp8266.handleClient();

  // mqtt things
  if (eeprom.mqtt_enabled) {
    if (!mqtt.connected()) {
      mqtt.connect(device_name, eeprom.mqtt_username, eeprom.mqtt_password);
#ifdef DEBUG
      Serial.println("MQTT RECONNECT");
#endif
    }
    if ((millis() - mqtt_interval) >= (MQTT_REFRESH * 60 * 1000UL)) {
      mqtt_interval = millis();
      mqtt.publish(MQTT_CLIMA_LOCALIP, WiFi.localIP().toString().c_str());
      snprintf(buf, sizeof(buf), "%.2f", temperature);
      mqtt.publish(MQTT_CLIMA_TEMPERATURE, buf);
      snprintf(buf, sizeof(buf), "%.2f", humidity);
      mqtt.publish(MQTT_CLIMA_HUMIDITY, buf);
#ifdef DEBUG
      Serial.println("MQTT REFRESH");
#endif
    }
    mqtt.loop();
  }

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
#ifdef DEBUG
      Serial.println("SAVE H");
#endif

#ifdef DAILY_FILE
      //  check if day changed
      if (now.tm_mday != last.tm_mday) {
        // gera nome do arquivo
        strftime(buf, sizeof(buf), "/%d%m%Y.csv", &yesterday);
        // write arquivo diario
        dump_csv(buf, (th_index < 24) ? 0 : (th_index - 25));
#ifdef DEBUG
        Serial.println("SAVE D");
#endif
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
#ifdef DEBUG
        Serial.println("SAVE M");
#endif
      }
    }
  }

  // loop end
}