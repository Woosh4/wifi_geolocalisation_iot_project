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
// const char* serverUrl = "http://172.20.10.8:8004/api/raw_scan";
// IP distante
const char* serverUrl = "http://vps-98cd652a.vps.ovh.net:8004/api/raw_scan";

void setup() {
  Serial.begin(115200); // pour debug
  if (TRANSMISSION_MODE == MODE_LORA) {
    Serial2.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);
  }
  delay(100);

  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();
  delay(100);

  if(TRANSMISSION_MODE == MODE_HTTP) setup_wifi();

  if(TRANSMISSION_MODE == MODE_HTTP){
    configTime(0, 0, "pool.ntp.org"); // pour time
    // Attendre que l'heure soit synchronisée
    int time_out_ntp = 20; // 0.5*20 = 10 secondes
    Serial.print("Synchronisation NTP");
    while ((time(NULL) < 100000) && (time_out_ntp > 0)) {
      delay(500);
      Serial.print(".");
      time_out_ntp--;
    }
    if(time_out_ntp > 0) Serial.println("\nHeure synchronisée !");
    else Serial.println("\nHeure non synchronisée.");
  }
  

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

String toHex(uint8_t* buf, int len){
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

// pour envoyer l'adresse mac et le rssi sur ttn : (6+1 bytes)*nb_wifi, prends le nombre de wifis à envoyer comme entrée, et le nombre total de wifis dans le scan
void sendViaLoRa(int nb_wifi, int nb_total_wifi) {
  int nb_wifi_vrai;
  nb_wifi_vrai = (nb_wifi < nb_total_wifi) ? nb_wifi : nb_total_wifi;
  uint8_t msg[7*nb_wifi_vrai];

  //trouver les nb_wifi_vrai wifis avec les rssis les plus grands, et les stocker dans le message
  int8_t prev_max = 127;
  int i_prev_max = -1;
  int8_t max = -128;
  int i_max;

  for(int i=0; i<nb_wifi_vrai; i++){
    for(int j=0; j<nb_total_wifi; j++){
      //nouveau max : forcément >= au max actuel, si == au précédent max, on mets d'abord l'index le plus élevé puis on descent
      // + ! WiFi.BSSID(j)[0] & 0x02 : bit "locally administered" à 0
      if((WiFi.RSSI(j) >= max) && ((WiFi.RSSI(j) < prev_max) || ((WiFi.RSSI(j) == prev_max) && (j < i_prev_max)) && (!(WiFi.BSSID(j)[0] & 0x02)))){
        i_max = j;
        max = WiFi.RSSI(j);
      }
    }
    memcpy(&msg[7*i], WiFi.BSSID(i_max), 6); // copie mac bssid
    msg[6+7*i] = (int8_t)WiFi.RSSI(i_max); // copie rssi
    prev_max = max;
    i_prev_max = i_max;
    max = -128;
  }

  // Convertir en hex
  String hexPayload = toHex(msg, 7*nb_wifi_vrai);
  Serial.println("Message en hex envoyé sur LoRa: " + hexPayload);

  // Envoi LoRa
  Serial2.print("AT+MSGHEX=\"");
  Serial2.print(hexPayload);
  Serial2.println("\"");
}

// Fonction pour envoyer en http
void sendViaHTTP(int index){ // index du wifi dans la liste "WiFi" de la librairie
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
  if(n == 0){
    Serial.println("Aucun réseau trouvé");
    delay(1000); // 1 seconde puis re scan
  }
  else{
    timestamp = (uint32_t)time(NULL);  // timestamp (4 bytes)
    String timeStr = getTimeString();

    Serial.println("Time -- SSID -- MAC -- RSSI");
    for(int i=0; i<n; i++){
      if(!(WiFi.BSSID(i)[0] & 0x02)){ //bit "locally administered" pour filtrer les partages de connexions
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

    if(TRANSMISSION_MODE == MODE_HTTP){ //Envoi http
      for(int i=0; i<n; i++){ //Envoi de chaque wifi 1 par 1
        if(!(WiFi.BSSID(i)[0] & 0x02)){ //bit "locally administered" pour filtrer les partages de connexions
          sendViaHTTP(i);
          Serial.println("Next wifi (http)");
          //petit delai
          delay(100);
        }
      }
      Serial.println("Envoi HTTP fini");
      delay(3000); //pause 3 secondes avant le prochain scan
    }

    else if(TRANSMISSION_MODE == MODE_LORA){ //Envoi LoRa
      sendViaLoRa(7, n); //envoi 7 wifi
      delay(200); //au cas ou si le module répond trop vite
      while(Serial2.available()){
        String resp = Serial2.readStringUntil('\n');
        resp.trim(); // enlever espaces, \r \n etc
        if (resp.length() > 0) {
          Serial.println("réponse Lora: " + resp);

          // vérif si pas/plus connecté pour reconnexion
          if(resp.equals("+MSGHEX: Please join network first")){
            Serial.println("Pas connecté au LoRa, reconnexion..");
            Serial2.println("AT+JOIN");
          }
        }
      }
      Serial.println("Envoi LoRa fini");
      delay(30000); // long délai 30s
    }
  WiFi.scanDelete(); // cleanup pour le prochain scan
  }
}
