#include <AsyncElegantOTA.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Husarnet.h>
#include <WiFi.h>
#include <Adafruit_Thermal.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define HTTP_PORT 8080

#define MQTT_TOPIC "dismore/print/rooster"
#define MQTT_SERVER "192.168.184.212"

char MQTT_CLIENTID[10];

WiFiClient net;
PubSubClient client(net);
Adafruit_Thermal printer(&Serial2);

String device_id, table_number, payment_method, invoice_id, service, customer_name, order_date, order_time, no_member, quantity, productname, notes;
bool clientconnect = true, clientloop = true, wl = true;
long lastReconnectAttempt, lastReconnectAttempt2;


// For GitHub Actions OTA deploment

// WiFi credentials
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASS;

// Husarnet credentials
const char *husarnetJoinCode = HUSARNET_JOINCODE;  // find at app.husarnet.com
const char *dashboardURL = "default";

AsyncWebServer server(HTTP_PORT);
const char *hostName = "dismoreprinter1";

// index.html available in "index_html" const String
extern const char index_html_start[] asm("_binary_src_index_html_start");
const String index_html = String((const char*)index_html_start);

void ota_init(){
  // ===============================================
  // Wi-Fi, OTA and Husarnet VPN configuration
  // ===============================================

  // remap default Serial (used by Husarnet logs) 
  Serial.begin(115200, SERIAL_8N1, 16, 17);  // from P3 & P1 to P16 & P17
  Serial1.begin(115200, SERIAL_8N1, 3, 1);  // remap Serial1 from P9 & P10 to P3 & P1

  Serial1.println("\r\n**************************************");
  Serial1.println("GitHub Actions OTA example");
  Serial1.println("**************************************\r\n");

  // Init Wi-Fi
  Serial1.printf("📻 1. Connecting to: %s Wi-Fi network ", ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    static int cnt = 0;
    delay(500);
    Serial1.print(".");
    cnt++;
    if (cnt > 10) {
      ESP.restart();
    }
  }

  Serial1.println(" done\r\n");

  // Init Husarnet P2P VPN service
  Serial1.printf("⌛ 2. Waiting for Husarnet to be ready ");

  Husarnet.selfHostedSetup(dashboardURL);
  Husarnet.join(husarnetJoinCode, hostName);
  Husarnet.start();

  // Before Husarnet is ready peer list contains: 
  // master (0000:0000:0000:0000:0000:0000:0000:0001)
  const uint8_t addr_comp[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  bool husarnetReady = 0;
  while (husarnetReady == 0) {
    Serial1.print(".");
    for (auto const &host : Husarnet.listPeers()) {
      if (host.first == addr_comp) {
        ;
      } else {
        husarnetReady = 1;
      }
    }
    delay(1000);
  }

  Serial1.println(" done\r\n");

  // define HTTP API for remote reset
  server.on("/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Reseting ESP32 after 1s ...");
    Serial1.println("Software reset on POST request");
    delay(2000);
    ESP.restart();
  });

  // Init OTA webserver (available under /update path)
  AsyncElegantOTA.begin(&server);
  server.begin();

  // ===============================================
  // PLACE YOUR APPLICATION CODE BELOW
  // ===============================================

  // Example webserver hosting table with known Husarnet Hosts

  Serial1.println("🚀 HTTP server started\r\n");
  Serial1.printf("Visit:\r\nhttp://%s:%d/\r\n\r\n", hostName, HTTP_PORT);

  Serial1.printf("Known hosts:\r\n");
  for (auto const &host : Husarnet.listPeers()) {
    Serial1.printf("%s (%s)\r\n", host.second.c_str(), host.first.toString().c_str());
  }
}

void print_data1() {
  printer.boldOn();
  printer.justify('L');
  printer.setSize('L');
  printer.println(table_number);
  //
  printer.justify('R');
  printer.setSize('S');
  printer.println("Payment by");
  //
  printer.boldOff();
  printer.println(payment_method);
  //
  printer.justify('L');
  printer.boldOn();
  printer.println("Invoice: " + invoice_id);
  printer.boldOff();
  //
  printer.println("");
  yield();
  //
  printer.print("Service ");
  printer.boldOn();
  printer.println("\t\t" + service);
  printer.boldOff();
  //
  printer.print("Date ");
  printer.boldOn();
  //
  printer.println("\t\t\t" + order_date);
  printer.boldOff();
  //
  printer.print("Customer ");
  printer.boldOn();
  printer.println("\t\t" + customer_name);
  printer.boldOff();
  //
  printer.print("Order Time ");
  printer.boldOn();
  printer.println("\t\t" + order_time);
  printer.boldOff();
  //
  printer.justify('C');
  printer.println("--------------------------------");
  //
  printer.justify('L');
  printer.boldOn();
  printer.setSize('S');
  printer.println("Order Details");
  printer.boldOff();
  //
  printer.println("");
  //
}

void print_data2() {
  printer.justify('L');
  printer.boldOn();
  printer.setSize('S');
  printer.println(productname);
  printer.justify('R');
  printer.println("x" + quantity);
  printer.boldOff();
  //
  if (notes != "") printer.println(notes);
}

void print_data3() {
  printer.justify('C');
  printer.println("--------------------------------");
  //
  printer.justify('L');
  printer.println("Member");
  printer.justify('R');
  printer.println(no_member);
  //
  printer.justify('C');
  printer.println("--------------------------------");
  //
  printer.println("");
  printer.println("");
  printer.println("");
}

void messageHandler(char* topic, byte* payload, unsigned int length) {
  String payloadStr = "";
  for (unsigned int i = 1; i < length - 1; i++) {
    if ((char)payload[i] != 92) {
      payloadStr += (char)payload[i];
      Serial.print((char)payload[i]);
    }
  }
  payloadStr.trim();
  StaticJsonDocument<4096> doc;
  DeserializationError error = deserializeJson(doc, payloadStr);
  if (error) {
    Serial1.print("JSON parsing error: ");
    Serial1.println(error.c_str());
    return;
  }
  //if (printer.hasPaper()) {
  const JsonArray& order_details = doc["order_details"];
  device_id = doc["device_id"].as<String>();
  table_number = doc["table_number"].as<String>();
  payment_method = doc["payment_method"].as<String>();
  invoice_id = doc["invoice_id"].as<String>();
  service = doc["service"].as<String>();
  customer_name = doc["customer_name"].as<String>();
  order_date = doc["order_date"].as<String>();
  order_time = doc["order_time"].as<String>();
  no_member = doc["no_member"].as<String>();
  for (const JsonObject& product : order_details) {
    productname = product["product_name"].as<String>();
    quantity = product["quantity"].as<String>();
    notes = product["notes"].as<String>();
  }
  Serial1.println("sukses bang");
  /*if (printer.hasPaper()) {
    //print_data1();
    //print_data2();
    p//rint_data3();
  }*/
  //}
}

void setupmqtt() {
  client.setServer(MQTT_SERVER, 1883);
  client.setBufferSize(4096);
  client.setCallback(messageHandler);

  Serial1.println("Connecting to MQTT");

  while (!client.connect(MQTT_CLIENTID, NULL, NULL, NULL, 1, 0, NULL, 0)) {
    Serial1.print(".");
    delay(100);
  }
  if (!client.connected()) {
    Serial1.println("Connect Timeout!");
    return;
  } else {
    digitalWrite(12, HIGH);
  }

  client.subscribe(MQTT_TOPIC, 1);
  Serial1.println("MQTT Connected!");
}

boolean reconnect() {
  if (client.connect(MQTT_CLIENTID, NULL, NULL, NULL, 1, 0, NULL, 0)) {
    client.subscribe(MQTT_TOPIC, 1);
  }
  return client.connected();
}

void setup(void) {
  ota_init();
  sprintf(MQTT_CLIENTID, "%s", "node1");
  Serial1.begin(9600);
  Serial2.begin(9600);
  pinMode(12, OUTPUT);
  printer.begin();
  printer.setTimes(1500, 1500);
  setupmqtt();
  lastReconnectAttempt = 0;
  lastReconnectAttempt2 = 0;
  Serial1.print(MQTT_CLIENTID);
  pinMode(2,OUTPUT);
}

void loop(void) {
  //if (printer.hasPaper()) {
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(12, LOW);
    if (!WiFi.isConnected()) setupmqtt();
    wl = false;
  } else {
    wl = true;
  }

  if (!client.connected()) {
    long now = millis();
    digitalWrite(12, LOW);
    clientconnect = false;
    if (now - lastReconnectAttempt > 1000) {
      lastReconnectAttempt = now;
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    clientconnect = true;
  }

  if (!client.loop()) {
    digitalWrite(12, LOW);
    long now_ = millis();
    clientloop = false;
    if (now_ - lastReconnectAttempt2 > 1000) {
      lastReconnectAttempt2 = now_;
      if (reconnect()) {
        lastReconnectAttempt2 = 0;
      }
    }
  } else {
    clientloop = true;
  }

  if (clientloop && clientconnect && wl) {
    digitalWrite(12, HIGH);
  }
}