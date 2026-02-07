#include <FS.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

#define FORMAT_SPIFFS_IF_FAILED true

// Config button pin for Armtronix (S1)
const int configButtonPin = 0; 

// Global variables for certificates
String ca_cert_str = "";
String client_cert_str = "";
String client_key_str = "";

const char* device_name = "ArmtronixQuadRelay";
const char* sw_version = "1.8.7";
const char* model = "Quad Relay Board";
const char* manufacturer = "Armtronix";

// MQTT Settings
char mqtt_server[40] = "";
char mqtt_user[20] = "";
char mqtt_password[40] = ""; 
char mqtt_port_str[6] = "8883"; 
unsigned int mqtt_port = 8883;   

// Relay configuration
const int relayPins[4] = {14, 13, 12, 4};
char relayNames[4][30] = {"Relay 1", "Relay 2", "Relay 3", "Relay 4"};
char relayClasses[4][20] = {"switch", "switch", "switch", "switch"};

// Buffers for WiFiManager labels and IDs
char relay_name_ids[4][10];
char relay_class_ids[4][10];
char relay_name_labels[4][40];
char relay_class_labels[4][40];

// Status flags
bool relayStates[4] = {false, false, false, false};
bool shouldStartPortal = false;

unsigned int mqtt_reconn_count = 0;
unsigned long tps = 0;
String mac_address;
char unique_id[40];

// MQTT Topics
char status_topic[60];
char portal_cmd_topic[70];
char relay_cfg_topics[4][100]; 
char relay_cmd_topics[4][60];
char relay_state_topics[4][60];

WiFiManager wifiManager;
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

// WiFiManager Parameters
WiFiManagerParameter custom_header_mqtt("<h2>MQTT Settings</h2>");
WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqtt_server, 40);
WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqtt_port_str, 6);
WiFiManagerParameter custom_mqtt_user("user", "MQTT User", mqtt_user, 20);
WiFiManagerParameter custom_mqtt_password("password", "MQTT Password", mqtt_password, 40, "type='password'");

WiFiManagerParameter custom_header_relays("<br><h2>Relay Settings</h2>");

WiFiManagerParameter* custom_relay_names[4];
WiFiManagerParameter* custom_relay_classes[4];

WiFiManagerParameter custom_hint_text("<p style='color:#666;font-size:0.8em;'>Classes: switch, light, fan, garage, outlet, lock, siren, valve</p>");

// TLS/Certificate Parameters
WiFiManagerParameter custom_tls_header("<br><h2>TLS Certificates (mTLS)</h2>");
WiFiManagerParameter custom_ca_cert("ca", "CA Certificate", "", 2000, "placeholder='Paste CA Cert here'");
WiFiManagerParameter custom_client_cert("cert", "Client Certificate", "", 2000, "placeholder='Paste Client Cert here'");
WiFiManagerParameter custom_client_key("key", "Private Key", "", 2000, "placeholder='Paste Private Key here'");
WiFiManagerParameter custom_clear_certs("clear", "Delete ALL stored certificates", "0", 2, "type='checkbox'");

// Script to change inputs to textareas for certificates
void injectCustomJS() {
  const char* script = "<script>"
    "window.onload = function() {"
    "  if(document.getElementsByName('ca')[0]) {"
    "    ['ca', 'cert', 'key'].forEach(function(name) {"
    "      var el = document.getElementsByName(name)[0];"
    "      if(el) el.outerHTML = '<textarea name=\"' + name + '\" style=\"width:100%;height:100px;font-family:monospace;\" placeholder=\"Paste ' + name.toUpperCase() + '\">' + el.value + '</textarea>';"
    "    });"
    "  }"
    "};"
    "</script>";
  static WiFiManagerParameter scriptParam(script);
  wifiManager.addParameter(&scriptParam);
}

void sendHADiscovery() {
  Serial.println("Sending Home Assistant Discovery...");
  for (int i = 0; i < 4; i++) {
    JsonDocument doc; 
    char r_id[30];
    sprintf(r_id, "%s_relay%d", unique_id, i);

    String dev_class = String(relayClasses[i]);
    dev_class.trim();
    dev_class.toLowerCase();

    String component = "switch";
    String payload_on = "ON";
    String payload_off = "OFF";
    String state_on = "ON";
    String state_off = "OFF";

    if (dev_class == "light") {
      component = "light";
    } 
    else if (dev_class == "fan") {
      component = "fan";
    } 
    else if (dev_class == "garage" || dev_class == "cover") {
      component = "cover";
      dev_class = "garage"; 
      payload_on = "OPEN";
      payload_off = "CLOSE";
      state_on = "open";
      state_off = "closed";
    }
    else if (dev_class == "lock") {
      component = "lock";
      payload_on = "LOCK";
      payload_off = "UNLOCK";
      state_on = "LOCKED";
      state_off = "UNLOCKED";
    }
    else if (dev_class == "siren") {
      component = "siren";
    }
    else if (dev_class == "valve") {
      component = "valve";
      payload_on = "OPEN";
      payload_off = "CLOSE";
      state_on = "open";
      state_off = "closed";
      doc["reports_position"] = false;
      doc["payload_open"] = "OPEN";
      doc["payload_close"] = "CLOSE";
      doc["state_open"] = "open";
      doc["state_closed"] = "closed";
    }
    else if (dev_class == "outlet") {
      component = "switch";
      doc["device_class"] = "outlet";
    }

    char discovery_topic[100];
    sprintf(discovery_topic, "homeassistant/%s/%s/relay%d/config", component.c_str(), unique_id, i);

    doc["name"] = relayNames[i];
    doc["stat_t"] = relay_state_topics[i];
    doc["cmd_t"] = relay_cmd_topics[i];
    doc["avty_t"] = status_topic;
    doc["pl_on"] = payload_on;
    doc["pl_off"] = payload_off;
    doc["stat_on"] = state_on;
    doc["stat_off"] = state_off;
    doc["uniq_id"] = r_id;

    if (component != "fan" && dev_class != "valve" && dev_class != "siren") {
       doc["dev_cla"] = dev_class;
    }
    
    JsonObject device = doc["device"].to<JsonObject>();
    device["ids"] = unique_id;
    device["name"] = device_name;
    device["mdl"] = model;
    device["mf"] = manufacturer;
    device["sw"] = sw_version;

    char buffer[1024];
    size_t n = serializeJson(doc, buffer);
    mqttClient.publish(discovery_topic, (const uint8_t*)buffer, n, true);
  }
}

void saveConfigParams() {
  Serial.println("Saving configuration to SPIFFS...");
  JsonDocument json; 
  
  json["mqtt_server"] = custom_mqtt_server.getValue();
  json["mqtt_port"] = custom_mqtt_port.getValue();
  json["mqtt_user"] = custom_mqtt_user.getValue();
  json["mqtt_password"] = custom_mqtt_password.getValue();

  for(int i=0; i<4; i++) {
    char keyName[15]; sprintf(keyName, "r_name_%d", i);
    char keyClass[15]; sprintf(keyClass, "r_class_%d", i);
    json[keyName] = custom_relay_names[i]->getValue();
    json[keyClass] = custom_relay_classes[i]->getValue();
  }

  if (String(custom_clear_certs.getValue()) == "1") {
    Serial.println("Clearing certificates...");
    json["ca_cert"] = "";
    json["client_cert"] = "";
    json["client_key"] = "";
  } else {
    String n_ca = String(custom_ca_cert.getValue());
    json["ca_cert"] = (n_ca.length() > 100) ? n_ca : ca_cert_str;

    String n_cert = String(custom_client_cert.getValue());
    json["client_cert"] = (n_cert.length() > 100) ? n_cert : client_cert_str;

    String n_key = String(custom_client_key.getValue());
    json["client_key"] = (n_key.length() > 100) ? n_key : client_key_str;
  }

  File configFile = SPIFFS.open("/config.json", "w");
  if (configFile) {
    serializeJson(json, configFile);
    configFile.close();
    Serial.println("Configuration saved.");
    
    Serial.println("Restarting device in 15 seconds...");
    delay(15000); 
    ESP.restart();
  }
}

void loadConfigParameters() {
  if (SPIFFS.exists("/config.json")) {
    File configFile = SPIFFS.open("/config.json", "r");
    if (configFile) {
      JsonDocument json; 
      DeserializationError error = deserializeJson(json, configFile);
      if (!error) {
        strcpy(mqtt_server, json["mqtt_server"] | "");
        strcpy(mqtt_port_str, json["mqtt_port"] | "8883");
        strcpy(mqtt_user, json["mqtt_user"] | "");
        strcpy(mqtt_password, json["mqtt_password"] | "");
        mqtt_port = atoi(mqtt_port_str);
        
        custom_mqtt_server.setValue(mqtt_server, 40);
        custom_mqtt_port.setValue(mqtt_port_str, 6);
        custom_mqtt_user.setValue(mqtt_user, 20);
        custom_mqtt_password.setValue(mqtt_password, 40);

        for(int i=0; i<4; i++) {
          char kn[15]; sprintf(kn, "r_name_%d", i);
          char kc[15]; sprintf(kc, "r_class_%d", i);
          String name = json[kn] | String("Relay " + String(i+1));
          String cls = json[kc] | "switch";
          
          strcpy(relayNames[i], name.c_str());
          strcpy(relayClasses[i], cls.c_str());
          
          if (custom_relay_names[i]) custom_relay_names[i]->setValue(relayNames[i], 30);
          if (custom_relay_classes[i]) custom_relay_classes[i]->setValue(relayClasses[i], 20);
        }
        
        ca_cert_str = json["ca_cert"].as<String>();
        client_cert_str = json["client_cert"].as<String>();
        client_key_str = json["client_key"].as<String>();
        
        Serial.println("Configuration loaded.");
      }
      configFile.close();
    }
  }
}

void receivedCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) message += (char)payload[i];
  
  if (String(topic) == portal_cmd_topic && message == "ON") {
    shouldStartPortal = true;
    return;
  }

  for (int i = 0; i < 4; i++) {
    if (String(topic) == relay_cmd_topics[i]) {
      String dev_class = String(relayClasses[i]);
      dev_class.toLowerCase();
      dev_class.trim();

      bool turnOn = false;
      bool turnOff = false;

      if (message.indexOf("UNLOCK") >= 0 || message.indexOf("OFF") >= 0 || message.indexOf("CLOSE") >= 0 || message.indexOf("TURN_OFF") >= 0) {
        turnOff = true;
      } else if (message.indexOf("LOCK") >= 0 || message.indexOf("ON") >= 0 || message.indexOf("OPEN") >= 0 || message.indexOf("TURN_ON") >= 0) {
        turnOn = true;
      }

      if (turnOn) {
        digitalWrite(relayPins[i], HIGH);
        relayStates[i] = true;
      } else if (turnOff) {
        digitalWrite(relayPins[i], LOW);
        relayStates[i] = false;
      }

      String state_msg = relayStates[i] ? "ON" : "OFF";
      if (dev_class == "garage" || dev_class == "cover" || dev_class == "valve") {
        state_msg = relayStates[i] ? "open" : "closed";
      } else if (dev_class == "lock") {
        state_msg = relayStates[i] ? "LOCKED" : "UNLOCKED";
      }

      mqttClient.publish(relay_state_topics[i], state_msg.c_str(), true);
    }
  }
}

void mqttConnect() {
  if (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection... ");
    if (mqttClient.connect(unique_id, mqtt_user, mqtt_password, status_topic, 1, 1, "offline", 1)) {
      Serial.println("connected!");
      for (int i = 0; i < 4; i++) mqttClient.subscribe(relay_cmd_topics[i]);
      mqttClient.subscribe(portal_cmd_topic);
      mqttClient.publish(status_topic, "online", true);
      sendHADiscovery(); 
    } else {
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- QuadRelay Controller Starting ---");

  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) return;

  uint64_t EfuseMac = ESP.getEfuseMac();
  mac_address = String((uint32_t)(EfuseMac >> 32), HEX) + String((uint32_t)EfuseMac, HEX);
  sprintf(unique_id, "QuadRelay_%s", mac_address.c_str());

  for(int i=0; i<4; i++) {
    sprintf(relay_name_ids[i], "rn%d", i);
    sprintf(relay_class_ids[i], "rc%d", i);
    sprintf(relay_name_labels[i], "Relay %d Name", i+1);
    sprintf(relay_class_labels[i], "Relay %d Device Class", i+1);
    
    custom_relay_names[i] = new WiFiManagerParameter(relay_name_ids[i], relay_name_labels[i], relayNames[i], 30);
    custom_relay_classes[i] = new WiFiManagerParameter(relay_class_ids[i], relay_class_labels[i], relayClasses[i], 20);
  }

  loadConfigParameters();

  sprintf(status_topic, "quadrelay/%s/status", mac_address.c_str());
  sprintf(portal_cmd_topic, "quadrelay/%s/portal/cmd", mac_address.c_str());
  for(int i=0; i<4; i++) {
    sprintf(relay_cmd_topics[i], "quadrelay/%s/relay%d/set", mac_address.c_str(), i);
    sprintf(relay_state_topics[i], "quadrelay/%s/relay%d/state", mac_address.c_str(), i);
  }

  wifiManager.addParameter(&custom_header_mqtt);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);
  
  wifiManager.addParameter(&custom_header_relays);
  for(int i=0; i<4; i++) {
    wifiManager.addParameter(custom_relay_names[i]);
    wifiManager.addParameter(custom_relay_classes[i]);
  }
  wifiManager.addParameter(&custom_hint_text);
  wifiManager.addParameter(&custom_tls_header);
  wifiManager.addParameter(&custom_ca_cert);
  wifiManager.addParameter(&custom_client_cert);
  wifiManager.addParameter(&custom_client_key);

  if (ca_cert_str.length() > 100) {
    custom_ca_cert.setValue("[STORED]", 8);
    custom_client_cert.setValue("[STORED]", 8);
    custom_client_key.setValue("[STORED]", 8);
    wifiManager.addParameter(&custom_clear_certs);
  }
  
  injectCustomJS(); 
  wifiManager.setSaveParamsCallback(saveConfigParams);

  if (digitalRead(configButtonPin) == LOW) {
    wifiManager.startConfigPortal(unique_id);
  } else {
    wifiManager.setConfigPortalTimeout(180); 
    if (!wifiManager.autoConnect(unique_id)) {
       delay(3000);
       ESP.restart();
    }
  }

  loadConfigParameters();

  for(int i=0; i<4; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  if (ca_cert_str.length() > 100) {
    wifiClient.setCACert(ca_cert_str.c_str());
    if (client_cert_str.length() > 100 && client_key_str.length() > 100) {
      wifiClient.setCertificate(client_cert_str.c_str());
      wifiClient.setPrivateKey(client_key_str.c_str());
    }
  } else {
    wifiClient.setInsecure();
  }

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(receivedCallback);
  mqttClient.setBufferSize(2048);
}

void loop() {
  if (shouldStartPortal) {
    wifiManager.startConfigPortal(unique_id);
    ESP.restart();
  }

  if (digitalRead(configButtonPin) == LOW) {
    unsigned long pressStart = millis();
    while (digitalRead(configButtonPin) == LOW) {
      if (millis() - pressStart > 3000) {
        shouldStartPortal = true;
        break;
      }
      delay(10);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) mqttConnect();
    mqttClient.loop();
  } else {
    static unsigned long lastWiFiCheck = 0;
    if (millis() - lastWiFiCheck > 60000) ESP.restart();
  }

  if (millis() - tps >= 30000) {
    tps = millis();
    if(mqttClient.connected()) mqttClient.publish(status_topic, "online", true);
  }
}
