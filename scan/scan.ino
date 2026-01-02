#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>
#include <base64.h>
#include <HTTPClient.h>

// envoi en lora / http wifi
#define MODE_LORA 1
#define MODE_HTTP 2
#define TRANSMISSION_MODE MODE_HTTP //MODE_HTTP; MODE_LORA

// === Configuration des broches du port série 2
#define LORA_TX 17  // TX2 ESP32 → RX module LoRaWAN
#define LORA_RX 16  // RX2 ESP32 ← TX module LoRaWAN

uint32_t timestamp = (uint32_t)time(NULL);  // timestamp (4 bytes)

const char* ssid = "iPhon de Alexcouille (2)";
const char* password = "bahenfaitnon";
// serverurl : IP locale
const char* serverUrl = "http://172.20.10.8:8004/api/raw_scan";
// IP distante
// const char* serverUrl = "http://vps-98cd652a.vps.ovh.net:8004/api/raw_scan";

void setup() {
  Serial.begin(115200); // pour debug
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
  // Connexion au wifi
  Serial.println();
  Serial.print("Connexion à : ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("WiFi connecté, IP de l'ESP : ");
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
//pour afficher l'heure de manière jolie HH:MM:SS
String getTimeString() {
  time_t now = time(nullptr);
  struct tm *tm_info = localtime(&now);

  char buffer[9];  // "HH:MM:SS" + null
  strftime(buffer, 9, "%H:%M:%S", tm_info);

  return String(buffer);
}

// pour envoyer l'heure, l'adresse mac et le rssi sur ttn : 4+6+1 bytes, prends l'index du wifi comme entrée
// a besoin que time soit mis à jour avant
void sendViaLoRa(int index) {
  uint8_t msg[11];

  // écrire timestamp (4 bytes)
  msg[0] = (timestamp >> 24) & 0xFF;
  msg[1] = (timestamp >> 16) & 0xFF;
  msg[2] = (timestamp >> 8)  & 0xFF;
  msg[3] = (timestamp >> 0)  & 0xFF;

  // MAC (6 bytes)
  uint8_t* mac = WiFi.BSSID(index);
  memcpy(&msg[4], mac, 6);

  // RSSI : 1 byte signé
  msg[10] = (int8_t)WiFi.RSSI(index);

  // Convertir en hex
  String hexPayload = toHex(msg, 11);

  Serial.println("Payload compact : " + hexPayload);

  // Envoi LoRa
  Serial2.print("AT+MSGHEX=\"");
  Serial2.print(hexPayload);
  Serial2.println("\"");
}

// Fonction pour envoyer en http
void sendViaHTTP(int index) {
  if(WiFi.status() == WL_CONNECTED){
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    // Crée un json pour le serveur
    // aussi envoi du ssid, pas nécessaire mais pratique pour du débug
    StaticJsonDocument<200> doc;
    doc["timestamp"] = timestamp;
    doc["mac"] = WiFi.BSSIDstr(index);
    doc["rssi"] = WiFi.RSSI(index);
    doc["ssid"] = WiFi.SSID(index);

    String requestBody;
    serializeJson(doc, requestBody);

    Serial.println("[HTTP] envoi : " + requestBody);
    int httpResponseCode = http.POST(requestBody);

    if(httpResponseCode > 0){
      String response = http.getString();
      Serial.println("[HTTP] code de réponse http: " + String(httpResponseCode));
      Serial.println("[HTTP] réponse serveur: " + response);
    } else {
      Serial.print("[HTTP] erreur, code http: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("[HTTP] erreur pas de connexion wifi");
    // reconnexion
    setup_wifi();
  }
}

void loop() {
  Serial.println("Scan WiFi...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("Aucun réseau trouvé");
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

    // reconnexion au wifi au cas où il se fait déconnecter
    if (TRANSMISSION_MODE == MODE_HTTP && WiFi.status() != WL_CONNECTED) {
        setup_wifi();
    }

    for(int i=0; i<n; i++){
      if(!(WiFi.BSSID(i)[0] & 0x02)){ //bit "locally administered" pour filtrer les partages de connexions

        if (TRANSMISSION_MODE == MODE_LORA) {
          sendViaLoRa(i);
          // Lecture de la réponse du LoRa-E5
          while (Serial2.available()) {
            String resp = Serial2.readStringUntil('\n');
            resp.trim();
            if (resp.length() > 0) {
              Serial.println("réponse Lora: " + resp);

              // vérif si pas connecté pour reconnexion
              if(resp.equals("+MSGHEX: Please join network first")){
                Serial.println("Pas connecté, reconnexion..");
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
          //petit delai
          delay(100);
        }
      }
    }
  }

  WiFi.scanDelete();
  
  if (TRANSMISSION_MODE == MODE_HTTP) {
      Serial.println("Scan envoyé. Pause de 3s pour le tracking...");
      delay(3000); // 3 secondes de pause seulement pour avoir un suivi fluide (entre chaque scan)
  } else {
      Serial.println("Scan LoRa envoyé. Pause 30s ...");
      delay(30000); // On garde 30s si on est en Lora pour libérer 99% du temps
  }
}
