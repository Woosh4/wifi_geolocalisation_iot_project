#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>
#include <HTTPClient.h>

// === CONFIGURATION ===
#define BUTTON_PIN 0       // Utilise le bouton BOOT de la carte (ou change pour un GPIO externe)
#define LED_PIN 2          // LED intégrée (souvent GPIO 2) pour feedback visuel

const char* ssid = "iPhon de Alexcouille (2)";
const char* password = "bahenfaitnon";

// IP de ton serveur (Attention à bien mettre l'IP locale de ton PC)
const char* serverUrl = "http://172.20.10.8:5000/api/raw_scan";

uint32_t current_timestamp = 0;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP); // Bouton actif BAS (LOW quand appuyé)
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // 1. Connexion WiFi
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();
  delay(100);
  setup_wifi();

  // 2. Synchro Heure
  configTime(0, 0, "pool.ntp.org");
  Serial.print("Synchro NTP");
  while (time(NULL) < 100000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nHeure OK. Prêt à scanner !");
  Serial.println(">>> APPUYEZ SUR LE BOUTON POUR CAPTURER UNE ZONE <<<");
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
    // Succès silencieux pour aller vite
  } else {
    Serial.printf("Erreur envoi (%d)\n", code);
  }
  http.end();
}

void loop() {
  // Détection appui bouton (LOW car INPUT_PULLUP)
  if (digitalRead(BUTTON_PIN) == LOW) {
    
    // Anti-rebond sommaire
    delay(50); 
    if(digitalRead(BUTTON_PIN) == LOW) {
      Serial.println("\n--- DÉBUT SCAN ZONE ---");
      digitalWrite(LED_PIN, HIGH); // Allumer LED pendant le travail

      int n = WiFi.scanNetworks();
      current_timestamp = (uint32_t)time(NULL); // On fige le temps du scan
      
      Serial.printf("%d réseaux trouvés. Envoi au serveur...\n", n);

      for (int i = 0; i < n; i++) {
         // Filtre simple pour éviter les trucs bizarres
         if(WiFi.SSID(i).length() > 0) {
            sendViaHTTP(i);
            delay(20); // Petit délai pour ne pas saturer le serveur
         }
      }

      WiFi.scanDelete();
      Serial.println("--- FIN ENVOI ---");
      Serial.println("Allez sur l'interface web pour géolocaliser cette mesure.");
      
      digitalWrite(LED_PIN, LOW); // Eteindre LED
      
      // Attendre que le bouton soit relâché pour ne pas boucler
      while(digitalRead(BUTTON_PIN) == LOW) delay(100);
    }
  }
}