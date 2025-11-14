#include <WiFi.h>
#include <ArduinoJson.h>

// === Configuration des broches du port série 2 (à adapter selon ton montage)
#define LORA_TX 17  // TX ESP32 → RX module LoRaWAN
#define LORA_RX 16  // RX ESP32 ← TX module LoRaWAN

void setup() {
  Serial.begin(115200);         // pour debug
  Serial2.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);  // communication LoRaWAN
  delay(100);

  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("\n=== Scan WiFi + Envoi LoRaWAN ===");
}

void loop() {
  Serial.println("Scan WiFi...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("Aucun réseau trouvé.");
  } else {
    // Trouver les trois réseaux avec le RSSI le plus fort
    int best1 = -1, best2 = -1, best3 = -1;
    for (int i = 0; i < n; i++) {
      if (best1 == -1 || WiFi.RSSI(i) > WiFi.RSSI(best1)) {
        best3 = best2;
        best2 = best1;
        best1 = i;
      } else if (best2 == -1 || WiFi.RSSI(i) > WiFi.RSSI(best2)) {
        best3 = best2;
        best2 = i;
      } else if (best3 == -1 ||  WiFi.RSSI(i) > WiFi.RSSI(best3)) {
        best3 = i;
      }
    }

    // Création du JSON
    StaticJsonDocument<256> doc;
    doc["AP1_MAC"] = WiFi.BSSIDstr(best1);
    doc["AP1_RSSI"] = WiFi.RSSI(best1);

    if (best2 >= 0) {
      doc["AP2_MAC"] = WiFi.BSSIDstr(best2);
      doc["AP2_RSSI"] = WiFi.RSSI(best2);
    }

    if (best3 >= 0) {
      doc["AP3_MAC"] = WiFi.BSSIDstr(best3);
      doc["AP3_RSSI"] = WiFi.RSSI(best3);
    }

    // Horodatage simplifié (UTC ou local selon besoin)
    time_t now = time(nullptr);
    char timestamp[25];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", gmtime(&now));
    doc["timestamp"] = timestamp;

    // Conversion en texte JSON
    String payload;
    serializeJson(doc, payload);

    // Envoi sur le port série LoRaWAN
    Serial2.println(payload);
    Serial.print("Envoyé via LoRaWAN : ");
    Serial.println(payload);
  }

  WiFi.scanDelete();
  delay(30000); // attendre 30s avant le prochain scan
}
