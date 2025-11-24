#include <WiFi.h>
#include <ArduinoJson.h>
#include <base64.h>

// === Configuration des broches du port série 2
#define LORA_TX 17  // TX2 ESP32 → RX module LoRaWAN
#define LORA_RX 16  // RX2 ESP32 ← TX module LoRaWAN

uint32_t timestamp = (uint32_t)time(NULL);  // timestamp (4 bytes)

const char* ssid = "iPhon de Alexcouille (2)";
const char* password = "bahenfaitnon";


void setup() {
  Serial.begin(115200);         // pour debug
  Serial2.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);  // communication LoRaWAN
  delay(100);

  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();
  delay(100);

  setup_wifi();

  configTime(0, 0, "pool.ntp.org"); // pour time

  Serial.println("\n=== Scan WiFi + Envoi LoRaWAN ===");
  Serial2.println("AT+JOIN");
  Serial.println("Setup OK");
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
//pour afficher l'heure joliement HH:MM:SS
String getTimeString() {
  time_t now = time(nullptr);
  struct tm *tm_info = localtime(&now);

  char buffer[9];  // "HH:MM:SS" + null
  strftime(buffer, 9, "%H:%M:%S", tm_info);

  return String(buffer);
}

// pour envoyer le temps, l'addresse MAC et Rssi sur ttn : 4+6+1 bytes, prend l'id du wifi en entrée
// a besoin que time soit mis à jour avant
void sendAP(int index) {
  uint8_t msg[11];

  // Écrire timestamp big-endian
  msg[0] = (timestamp >> 24) & 0xFF;
  msg[1] = (timestamp >> 16) & 0xFF;
  msg[2] = (timestamp >> 8)  & 0xFF;
  msg[3] = (timestamp >> 0)  & 0xFF;

  // Copier MAC (6 bytes)
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

void loop() {
  Serial.println("Scan WiFi...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("Aucun réseau trouvé.");
  } else {
    timestamp = (uint32_t)time(NULL);  // timestamp (4 bytes)
    String timeStr = getTimeString();

    Serial.println("Time | SSID | MAC | RSSI");
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

    for(int i=0; i<n; i++){
      if(!(WiFi.BSSID(i)[0] & 0x02)){
        sendAP(i);

        // Lecture de la réponse du LoRa-E5
        while (Serial2.available()) {
          String resp = Serial2.readStringUntil('\n');
          resp.trim();
          if (resp.length() > 0) {
            Serial.println("Réponse LoRa-E5: " + resp);

            // vérif si pas connecté
            if(resp.equals("+MSGHEX: Please join network first")){
              Serial.println("Pas connecté. reconnexion..");
              Serial2.println("AT+JOIN");
            }
          }
        }
        Serial.println("Wifi suivant");
        delay(30000);
      }
    }
  }

  WiFi.scanDelete();
  Serial.println("Wifis tous envoyés, prochaine mesure dans 30 sec");
  delay(30000); // attendre 30s avant le prochain scan
}
