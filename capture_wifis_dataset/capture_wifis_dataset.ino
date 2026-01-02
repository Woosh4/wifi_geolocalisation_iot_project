#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>
#include <HTTPClient.h>

// === CONFIGURATION ===
#define BUTTON_PIN 0       // Utilise le bouton BOOT de la carte
#define LED_PIN 2          // LED intégrée GPIO 2 pour affichage en direct (allumée pendant le scan puis s'éteint)

//pour la connexion en wifi
const char* ssid = "iPhon de Alexcouille (2)";
const char* password = "bahenfaitnon";

//IP du serveur local
const char* serverUrl = "http://172.20.10.8:8004/api/raw_scan";
//serveur distant
//const char* serverUrl = "http://vps-98cd652a.vps.ovh.net:8004/api/raw_scan";

uint32_t current_timestamp = 0;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP); //bouton actif bas
  pinMode(LED_PIN, OUTPUT); //led
  digitalWrite(LED_PIN, LOW);

  //connexion au wifi
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();
  delay(100);
  setup_wifi();

  //synchro de l'heure
  configTime(0, 0, "pool.ntp.org");
  Serial.print("Synchro NTP");
  while (time(NULL) < 100000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nSynchro heure OK. Scan prêt");
  Serial.println("Appuyer sur le bouton boot pour lancer un scan");
}

void setup_wifi() {
  Serial.print("Connexion WiFi à ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nConnecté ! IP: " + WiFi.localIP().toString());
}

void sendViaHTTP(int index) {
  if(WiFi.status() != WL_CONNECTED) setup_wifi();

  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<200> doc;
  doc["timestamp"] = current_timestamp; // Le même timestamp pour tout le scan
  doc["ssid"] = WiFi.SSID(index);
  doc["mac"] = WiFi.BSSIDstr(index);
  doc["rssi"] = WiFi.RSSI(index);

  String requestBody;
  serializeJson(doc, requestBody);

  int code = http.POST(requestBody);
  if(code > 0) {
    //OK
  } else {
    Serial.printf("Erreur envoi (%d)\n", code);
  }
  http.end();
}

void loop() {
  // Détection appui bouton (LOW car INPUT_PULLUP)
  if (digitalRead(BUTTON_PIN) == LOW) {
    
    //anti rebond
    delay(50); 
    if(digitalRead(BUTTON_PIN) == LOW) {
      Serial.println("\n--- DEBUT DU SCAN ---");
      digitalWrite(LED_PIN, HIGH); // Allumer LED pendant le scan

      int n = WiFi.scanNetworks();
      current_timestamp = (uint32_t)time(NULL); // capture du temsp pour ce scan
      
      Serial.printf("%d réseaux trouvés. Envoi vers le serveur...\n", n);

      for (int i = 0; i < n; i++) {
         // Filtre simple pour éviter les trucs bizarres
         if(WiFi.SSID(i).length() > 0) {
            sendViaHTTP(i);
            delay(20); //petit délai pour le serveur
         }
      }

      WiFi.scanDelete();
      Serial.println("--- FIN DE L'ENVOI ---");
      Serial.println("Aller sur l'interface web pour géolocaliser la mesure.");
      
      digitalWrite(LED_PIN, LOW); // Eteindre LED : capture finie
      
      // Attendre que le bouton soit relaché pour ne pas faire plusieurs envois à la suite
      while(digitalRead(BUTTON_PIN) == LOW) delay(100);
    }
  }
}