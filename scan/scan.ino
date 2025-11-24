#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>
#include <base64.h>
#include <HTTPClient.h>

// envoi en lora / http wifi
#define MODE_LORA 1
#define MODE_HTTP 2
#define TRANSMISSION_MODE MODE_HTTP

// === Configuration des broches du port série 2
#define LORA_TX 17  // TX2 ESP32 → RX module LoRaWAN
#define LORA_RX 16  // RX2 ESP32 ← TX module LoRaWAN

uint32_t timestamp = (uint32_t)time(NULL);  // timestamp (4 bytes)

const char* ssid = "iPhon de Alexcouille (2)";
const char* password = "bahenfaitnon";
const char* serverUrl = "http://192.168.1.25:5000/api/geoloc";

void setup() {
  Serial.begin(115200);         // pour debug
  if (TRANSMISSION_MODE == MODE_LORA) {
    Serial2.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);
  }
  delay(100);

  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();
  delay(100);

  setup_wifi();

  configTime(0, 0, "pool.ntp.org"); // pour time

  // Attendre que l'heure soit synchronisée
  Serial.print("Synchronisation NTP");
  while (time(NULL) < 100000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nHeure synchronisée !");

  if (TRANSMISSION_MODE == MODE_LORA) {
    Serial.println("\n=== Scan WiFi + Envoi LoRaWAN ===");
    Serial2.println("AT+JOIN");
    Serial.println("Setup LORA OK");
  } else {
    Serial.println("\n=== Scan WiFi + Envoi HTTP Local ===");
    Serial.println("Setup HTTP OK");
  }
  delay(200);
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("WiFi connected - ESP IP address: ");
  Serial.println(WiFi.localIP());
}

String toHex(uint8_t* buf, int len) {
  const char hex[] = "0123456789ABCDEF";
  String out = "";
  for (int i = 0; i < len; i++) {
    out += hex[(buf[i] >> 4) & 0xF];
    out += hex[buf[i] & 0xF];
  }
  return out;
}
//to print time in a nice way HH:MM:SS
String getTimeString() {
  time_t now = time(nullptr);
  struct tm *tm_info = localtime(&now);

  char buffer[9];  // "HH:MM:SS" + null
  strftime(buffer, 9, "%H:%M:%S", tm_info);

  return String(buffer);
}

// to send time, mac address and rssi on ttn : 4+6+1 bytes, takes the wifi id as input
// a besoin que time soit mis à jour avant
void sendViaLoRa(int index) {
  uint8_t msg[11];

  // Écrire timestamp big-endian
  msg[0] = (timestamp >> 24) & 0xFF;
  msg[1] = (timestamp >> 16) & 0xFF;
  msg[2] = (timestamp >> 8)  & 0xFF;
  msg[3] = (timestamp >> 0)  & 0xFF;

  // Copy MAC (6 bytes)
  uint8_t* mac = WiFi.BSSID(index);
  memcpy(&msg[4], mac, 6);

  // RSSI : 1 byte signed
  msg[10] = (int8_t)WiFi.RSSI(index);

  // Convertir en hex
  String hexPayload = toHex(msg, 11);

  Serial.println("Payload compact : " + hexPayload);

  // Envoi LoRa
  Serial2.print("AT+MSGHEX=\"");
  Serial2.print(hexPayload);
  Serial2.println("\"");
}

// Function to send via HTTP
void sendViaHTTP(int index) {
  if(WiFi.status() == WL_CONNECTED){
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    // Create JSON for the server
    // Also send the scanned ssid for debug
    StaticJsonDocument<200> doc;
    doc["timestamp"] = timestamp;
    doc["mac_ap"] = WiFi.BSSIDstr(index);
    doc["rssi"] = WiFi.RSSI(index);
    doc["ssid_ap"] = WiFi.SSID(index); 

    String requestBody;
    serializeJson(doc, requestBody);

    Serial.println("[HTTP] sending : " + requestBody);
    int httpResponseCode = http.POST(requestBody);

    if(httpResponseCode > 0){
      String response = http.getString();
      Serial.println("[HTTP] response code: " + String(httpResponseCode));
      Serial.println("[HTTP] server response: " + response);
    } else {
      Serial.print("[HTTP] send error: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("[HTTP] error : wifi disconnected");
    // try to reconnect
    setup_wifi();
  }
}

void loop() {
  Serial.println("Scan WiFi...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("No networks found.");
  } else {
    timestamp = (uint32_t)time(NULL);  // timestamp (4 bytes)
    String timeStr = getTimeString();

    Serial.println("Time -- SSID -- MAC -- RSSI");
    for(int i=0; i<n; i++){
      if(!(WiFi.BSSID(i)[0] & 0x02)){
        Serial.print(timeStr);
        Serial.print("--");
        Serial.print(WiFi.SSID(i));
        Serial.print("--");
        Serial.print(WiFi.BSSIDstr(i));
        Serial.print("--");
        Serial.println(WiFi.RSSI(i));
      }
    }
    Serial.println("=========================");

    // reconnect wifi in case the scan cut off the connection
    if (TRANSMISSION_MODE == MODE_HTTP && WiFi.status() != WL_CONNECTED) {
        setup_wifi();
    }

    for(int i=0; i<n; i++){
      if(!(WiFi.BSSID(i)[0] & 0x02)){

        if (TRANSMISSION_MODE == MODE_LORA) {
          sendViaLoRa(i);
          // Lecture de la réponse du LoRa-E5
          while (Serial2.available()) {
            String resp = Serial2.readStringUntil('\n');
            resp.trim();
            if (resp.length() > 0) {
              Serial.println("response LoRa-E5: " + resp);

              // vérif si pas connecté
              if(resp.equals("+MSGHEX: Please join network first")){
                Serial.println("Not connected. reconnecting..");
                Serial2.println("AT+JOIN");
                i--; // pour ré-envoyer la donnée
              }
            }
          }
          Serial.println("Next wifi (lora)");
          delay(30000);
        }

        else{ // Mode HTTP
          sendViaHTTP(i);
          Serial.println("Next wifi (http)");
          //short delay
          delay(100);
        }
      }
    }
  }

  WiFi.scanDelete();
  Serial.println("All wifis sent, next scan in 30 sec");
  delay(30000); // attendre 30s avant le prochain scan
}
