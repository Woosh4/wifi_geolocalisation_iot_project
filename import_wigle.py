import requests
import csv
import base64

# === CONFIGURATION ===
# Remplace par ta chaîne "Encoded for use" trouvée sur le site WiGLE
# Attention: Ne mets pas "Basic " devant, juste la chaîne
WIGLE_AUTH = "QUlENDM2NDRhZmExN2MyYTYwYzBlNjIyYWNmNTk0OTY5YTU6Y2NmNDYwODRlMjc0N2E1OTVmMTc2ZjA0ZjMzYzI3MzQ" 

# Définis ta zone géographique (Exemple: Une zone à Paris)
# Utilise boundingbox.klokantech.com pour trouver tes valeurs
# PARIS :
# LAT_MIN = 48.844
# LAT_MAX = 48.8479
# LON_MIN = 2.3524
# LON_MAX = 2.3642
# YVELINES :
LAT_MIN = 48.8097
LAT_MAX = 48.8175
LON_MIN = 1.8985
LON_MAX = 1.9222

OUTPUT_FILE = "local_wifi_db_wigle.csv"

def fetch_wigle_data():
    url = "https://api.wigle.net/api/v2/network/search"
    
    headers = {
        "Authorization": f"Basic {WIGLE_AUTH}",
        "Accept": "application/json"
    }
    
    # Paramètres de recherche
    params = {
        "onlymine": "false",       # Chercher dans toute la base
        "latrange1": LAT_MIN,      # Latitude min
        "latrange2": LAT_MAX,      # Latitude max
        "longrange1": LON_MIN,     # Longitude min
        "longrange2": LON_MAX,     # Longitude max
        "freenet": "false",        # Tout type de réseau (pas que les gratuits)
        "paynet": "false", 
        "resultsPerPage": 100      # Max 100 par page pour la version gratuite de base
    }

    print("Interrogation de WiGLE...")
    try:
        response = requests.get(url, headers=headers, params=params)
        
        if response.status_code == 200:
            data = response.json()
            
            if data["success"]:
                results = data["results"]
                count = len(results)
                print(f"{count} réseaux trouvés !")
                
                # Sauvegarde en CSV
                save_to_csv(results)
            else:
                print("Erreur API :", data)
        elif response.status_code == 401:
            print("Erreur 401 : Authentification refusée. Vérifie ton token 'Encoded for use'.")
        else:
            print(f"Erreur HTTP {response.status_code}")
            
    except Exception as e:
        print(f"Erreur script : {e}")

def save_to_csv(networks):
    # On ne garde que ce qui est utile pour la géoloc
    # netid = MAC Address
    # ssid = Nom
    # trilat/trilong = Position triangulée par WiGLE
    
    with open(OUTPUT_FILE, mode='w', newline='', encoding='utf-8') as file:
        writer = csv.writer(file)
        # En-têtes
        writer.writerow(["mac", "ssid", "lat", "lon", "rssi_avg"])
        
        for net in networks:
            writer.writerow([
                net.get("netid"),    # L'adresse MAC (BSSID)
                net.get("ssid"),     # Le nom du réseau
                net.get("trilat"),   # Latitude connue
                net.get("trilong"),  # Longitude connue
                net.get("rssi")      # Signal moyen (pour info)
            ])
            
    print(f"Base de données sauvegardée dans '{OUTPUT_FILE}'")

if __name__ == "__main__":
    fetch_wigle_data()