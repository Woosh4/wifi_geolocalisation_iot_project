import uvicorn
from fastapi import FastAPI, Request, HTTPException
from fastapi.templating import Jinja2Templates
from fastapi.responses import HTMLResponse
from pydantic import BaseModel
import json
import os
import math
import time
from collections import defaultdict, deque

# ==========================================
# 1. CONFIGURATION DU SERVEUR
# ==========================================

HOST_IP = "0.0.0.0"      # Écoute sur toutes les interfaces réseau
PORT = 8004              # Port du serveur (8004 résservé pour le serveur ovh)
DB_FILE = "database_test_yvelines.json"
# DB_FILE = "database_wifi_clean.json"

# --- Paramètres de l'Algorithme ---
K_NEIGHBORS = 5          # Nombre de voisins à considérer (k-NN)
HISTORY_SIZE = 100       # Nombre de positions passées à garder en mémoire

# --- Mode de Communication ---
MODE_WIFI = "WIFI"
MODE_LORA = "LORA"
CURRENT_MODE = MODE_WIFI

# ==========================================
# 2. INITIALISATION & MÉMOIRE
# ==========================================

app = FastAPI(title="Traqueur de position ESP32")
templates = Jinja2Templates(directory="templates")

# Base de données des empreintes (chargée au démarrage)
#C'est une liste de dictionnaires : chaque élément contient la position géographique,
#et un dictionnaire du type mac:rssi
fingerprint_db = []

# Buffer pour le mode WiFi (car l'ESP32 envoie les réseaux un par un)
# Structure : { "MAC_ADDRESS": RSSI, ... }
current_wifi_buffer = {}
last_buffer_update = 0

# Historique des positions calculées
# On utilise 'deque' pour limiter automatiquement la taille (FIFO)
position_history = deque(maxlen=HISTORY_SIZE)

# Modèle de données reçu depuis l'ESP32 (Mode WiFi)
class WifiScanData(BaseModel):
    timestamp: int
    ssid: str
    mac: str
    rssi: int

# ==========================================
# 3. FONCTIONS
# ==========================================

def load_database():
    """
    Charge le fichier JSON et regroupe les scans par timestamp.
    Cela crée des 'Empreintes' complètes pour la comparaison.
    """
    global fingerprint_db
    if not os.path.exists(DB_FILE):
        print(f"Erreur : Fichier {DB_FILE} introuvable.")
        return

    try:
        with open(DB_FILE, 'r') as f:
            raw_data = json.load(f)
        
        # Regroupement : Un timestamp = Une position unique (Lat/Lon/Etage)
        # dictionnaire avec lat,lon,etage, avec dedans un autre dict pour mac:rssi, pour chaque rssi
        grouped = defaultdict(lambda: {'lat': 0, 'lon': 0, 'floor': 0, 'aps': {}})
        
        for entry in raw_data:
            ts = entry['timestamp']
            grouped[ts]['lat'] = entry['latitude']
            grouped[ts]['lon'] = entry['longitude']
            grouped[ts]['floor'] = entry['floor']
            # On stocke les MACs et RSSI dans un sous-dictionnaire
            grouped[ts]['aps'][entry['mac']] = entry['rssi']

        fingerprint_db = list(grouped.values())
        print(f"Base de données chargée : {len(fingerprint_db)} points de référence.")
        
    except Exception as e:
        print(f"Erreur lors du chargement de la BDD : {e}")

#Calcul de la position estimée
def algorithm_wknn(live_aps):
    """
    Algorithme Weighted k-Nearest Neighbors (k-NN Pondéré).
    1. Compare le scan actuel avec toute la BDD.
    2. Sélectionne les K points les plus ressemblants.
    3. Calcule une moyenne pondérée pour les coordonnées
    """
    if not fingerprint_db or not live_aps:
        return None

    distances = []

    # 1, calcul de la "distance" entre la mesure et chaque point de la base de données:
    # en faisant la somme de la différence carrée des rssi, puis en prenant la racine de ce nombre.
    for fp in fingerprint_db:
        dist_sq_sum = 0
        match_count = 0
        
        for mac, rssi_live in live_aps.items():
            # Si le routeur du live existe dans l'empreinte stockée
            if mac in fp['aps']:
                rssi_db = fp['aps'][mac]
                # Différence au carré
                dist_sq_sum += (rssi_live - rssi_db) ** 2
                #nombre de points communs
                match_count += 1
            else:
                # Pénalité si le routeur est manquant dans la base (100 dBm de différence = 100**2)
                dist_sq_sum += 10000

        # Si aucun routeur en commun, distance infinie
        if match_count == 0:
            final_dist = 1e9
        else:
            final_dist = math.sqrt(dist_sq_sum)
        
        #stockage des distances pour chaque point de la base de donnée
        distances.append({
            "dist": final_dist,
            "lat": fp['lat'],
            "lon": fp['lon'],
            "floor": fp['floor']
        })

    # 2. Trouver les plus proches (je prends les K_NEIGHBORS plus proches,
    # en soit on pourrait prendre tous les points mais cela ne serait pas forcément plus précis et
    # serait plus couteux en calcul / temps)
    
    # Tri du plus petit écart au plus grand
    distances.sort(key=lambda x: x["dist"])
    k_nearest = distances[:K_NEIGHBORS]
    
    # 3. Calcul de la position estimée
    weight_sum = 0
    lat_sum = 0; lon_sum = 0; floor_sum = 0
    
    # Pour calculer l'incertitude géographique plus tard
    coords_neighbors = [] 

    for item in k_nearest:
        # Poids = Inverse de la distance ( +0.001 pour éviter division par zéro)
        # donc si la distance est petite le poids est grand.
        w = 1 / (item["dist"] + 0.001)
        
        lat_sum += item["lat"] * w
        lon_sum += item["lon"] * w
        floor_sum += item["floor"] * w
        weight_sum += w
        
        coords_neighbors.append((item["lat"], item["lon"]))

    if weight_sum == 0: return None

    # Résultat final estimé
    est_lat = lat_sum / weight_sum
    est_lon = lon_sum / weight_sum
    est_floor = round(floor_sum / weight_sum)

    # 4. Calcul d'incertitude
    # Par le calcul de l'écart avec les points connus : plus on est proches d'un point connu plus l'incertitude est faible
    #(seulement sur les K_NEIGHBORS points plus proches), puis moyenne de ces écarts
    uncertainty_score = 0
    for n_lat, n_lon in coords_neighbors:
        # Distance approximative entre le voisin et le point estimé
        d = math.sqrt((n_lat - est_lat)**2 + (n_lon - est_lon)**2)
        uncertainty_score += d
    
    # Conversion degrés -> mètres (approx pour la France : 111km par degré)
    accuracy_meters = (uncertainty_score / K_NEIGHBORS) * 111000 

    return {
        "timestamp": int(time.time()), # On ajoute l'heure du calcul
        "lat": est_lat,
        "lon": est_lon,
        "floor": est_floor,
        "accuracy": accuracy_meters,
        "details": [round(x['dist'], 1) for x in k_nearest] # Pour debug
    }

# ==========================================
# 4. HTTP et API
# ==========================================

@app.on_event("startup")
async def start_app():
    load_database()
    print(f"Serveur démarré en mode : {CURRENT_MODE}")

@app.get("/", response_class=HTMLResponse)
async def get_map_page(request: Request):
    """Affiche la carte"""
    return templates.TemplateResponse("map.html", {"request": request})

# Réception (Mode WiFi HTTP) des données de l'ESP32
@app.post("/api/raw_scan")
async def receive_wifi_scan(data: WifiScanData):
    """
    L'ESP32 envoie les réseaux un par un. On les stocke dans un buffer.
    """
    global current_wifi_buffer, last_buffer_update
    
    if CURRENT_MODE != MODE_WIFI:
        return {"status": "ignored", "reason": "Server in LoRa mode"}

    # Si le buffer est vieux (> 2s), c'est un nouveau scan, on supprime l'ancien
    if time.time() - last_buffer_update > 2.0:
        current_wifi_buffer = {} 

    current_wifi_buffer[data.mac] = data.rssi
    last_buffer_update = time.time()
    
    return {"status": "buffered"}

# Réception (Mode LoRaWAN) des données avec le webhook ttn (pas implémenté)
@app.post("/api/lora_uplink")
async def receive_lora_uplink(request: Request):
    """
    Récupère les données avec le webhook de The Things Network
    """
    if CURRENT_MODE != MODE_LORA:
        raise HTTPException(status_code=400, detail="Server in WiFi mode")
    
    return {"status": "received"}

# Calcul et affichage de la position et de l'historique
@app.get("/api/get_position")
async def get_position_api():
    """
    Appelé périodiquement par la page web map.html
    Vérifie si des données récentes sont là.
    fais le calcul.
    màj de l'historique.
    Renvoie le statut, position, erreur, historique au navigateur
    """
    # Timeout : Si pas de données depuis 10s, on est hors ligne (mode HTTP principalement)
    if time.time() - last_buffer_update > 10.0:
        return {"status": "offline"}

    # Calcul de la position
    estimated_pos = algorithm_wknn(current_wifi_buffer)

    if estimated_pos:
        # Ajout à l'historique (pour tracer le chemin)
        # On évite les doublons si la position n'a pas changé depuis la dernière requête
        if not position_history or (position_history[-1]['timestamp'] != estimated_pos['timestamp']):
             position_history.append(estimated_pos)

        return {
            "status": "tracking",
            "current": estimated_pos,
            "history": list(position_history) # On renvoie tout l'historique
        }
    else:
        return {"status": "calibrating"} # Pas assez de données ou pas de correspondance

if __name__ == "__main__":
    uvicorn.run("server_geoloc:app", host=HOST_IP, port=PORT, reload=True)