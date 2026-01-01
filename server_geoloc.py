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
PORT = 5000              # Port du serveur
DB_FILE = "database_test_yvelines.json"

# --- Paramètres de l'Algorithme ---
K_NEIGHBORS = 3          # Nombre de voisins à considérer (k-NN)
HISTORY_SIZE = 100       # Nombre de positions passées à garder en mémoire

# --- Mode de Communication ---
MODE_WIFI = "WIFI"       # L'ESP32 envoie des requêtes HTTP directes
MODE_LORA = "LORA"       # Les données arrivent via un webhook (ex: TTN)
CURRENT_MODE = MODE_WIFI # <--- CHANGER ICI LE MODE (WIFI ou LORA)

# ==========================================
# 2. INITIALISATION & MÉMOIRE
# ==========================================

app = FastAPI(title="ESP32 Indoor Tracking")
templates = Jinja2Templates(directory="templates")

# Base de données des empreintes (chargée au démarrage)
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
# 3. FONCTIONS LOGIQUES (MOTEUR)
# ==========================================

def load_database():
    """
    Charge le fichier JSON et regroupe les scans par timestamp.
    Cela crée des 'Scènes' ou 'Empreintes' complètes pour la comparaison.
    """
    global fingerprint_db
    if not os.path.exists(DB_FILE):
        print(f"⚠️ Erreur : Fichier {DB_FILE} introuvable.")
        return

    try:
        with open(DB_FILE, 'r') as f:
            raw_data = json.load(f)
        
        # Regroupement : Un timestamp = Une position unique (Lat/Lon/Etage)
        grouped = defaultdict(lambda: {'lat': 0, 'lon': 0, 'floor': 0, 'aps': {}})
        
        for entry in raw_data:
            ts = entry['timestamp']
            grouped[ts]['lat'] = entry['latitude']
            grouped[ts]['lon'] = entry['longitude']
            grouped[ts]['floor'] = entry['floor']
            # On stocke les MACs et RSSI dans un sous-dictionnaire
            grouped[ts]['aps'][entry['mac']] = entry['rssi']

        fingerprint_db = list(grouped.values())
        print(f"✅ Base de données chargée : {len(fingerprint_db)} points de référence.")
        
    except Exception as e:
        print(f"❌ Erreur lors du chargement de la BDD : {e}")

def algorithm_wknn(live_aps):
    """
    Algorithme Weighted k-Nearest Neighbors (k-NN Pondéré).
    1. Compare le scan actuel avec toute la BDD.
    2. Sélectionne les K points les plus ressemblants.
    3. Calcule une moyenne pondérée (plus on ressemble, plus on a de poids).
    """
    if not fingerprint_db or not live_aps:
        return None

    distances = []

    # --- Étape A : Calcul de la "distance" avec chaque point de la base ---
    for fp in fingerprint_db:
        dist_sq_sum = 0
        match_count = 0
        
        for mac, rssi_live in live_aps.items():
            # Si le routeur du live existe dans l'empreinte stockée
            if mac in fp['aps']:
                rssi_db = fp['aps'][mac]
                # Différence au carré (Euclidien)
                dist_sq_sum += (rssi_live - rssi_db) ** 2
                match_count += 1
            else:
                # Pénalité si le routeur est manquant dans la base (100 dBm de diff)
                dist_sq_sum += (100) ** 2 

        # Si aucun routeur en commun, on rejette ce point (distance infinie)
        if match_count == 0:
            final_dist = 1e9
        else:
            final_dist = math.sqrt(dist_sq_sum)
        
        distances.append({
            "dist": final_dist,
            "lat": fp['lat'],
            "lon": fp['lon'],
            "floor": fp['floor']
        })

    # --- Étape B : Trouver les K plus proches ---
    # Tri du plus petit écart au plus grand
    distances.sort(key=lambda x: x["dist"])
    k_nearest = distances[:K_NEIGHBORS]
    
    # --- Étape C : Barycentre pondéré ---
    weight_sum = 0
    lat_sum = 0; lon_sum = 0; floor_sum = 0
    
    # Pour calculer l'incertitude géographique plus tard
    coords_neighbors = [] 

    for item in k_nearest:
        # Poids = Inverse de la distance ( +0.1 pour éviter division par zéro)
        # Si distance est petite (grande ressemblance), le poids est grand.
        w = 1 / (item["dist"] + 0.1)
        
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

    # --- Étape D : Estimation de la précision (Incertitude) ---
    # On calcule la dispersion géographique des voisins utilisés
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
# 4. API (ROUTES)
# ==========================================

@app.on_event("startup")
async def start_app():
    load_database()
    print(f"🚀 Serveur démarré en mode : {CURRENT_MODE}")

@app.get("/", response_class=HTMLResponse)
async def get_map_page(request: Request):
    """Affiche la carte (Front-end)"""
    return templates.TemplateResponse("map.html", {"request": request})

# --- Route Réception (Mode WiFi HTTP) ---
@app.post("/api/raw_scan")
async def receive_wifi_scan(data: WifiScanData):
    """
    L'ESP32 envoie les réseaux un par un. On les stocke dans un tampon (buffer).
    """
    global current_wifi_buffer, last_buffer_update
    
    if CURRENT_MODE != MODE_WIFI:
        return {"status": "ignored", "reason": "Server in LoRa mode"}

    # Si le tampon est vieux (> 2s), c'est un nouveau scan, on vide l'ancien
    if time.time() - last_buffer_update > 2.0:
        current_wifi_buffer = {} 

    current_wifi_buffer[data.mac] = data.rssi
    last_buffer_update = time.time()
    
    return {"status": "buffered"}

# --- Route Réception (Mode LoRaWAN - Placeholder) ---
@app.post("/api/lora_uplink")
async def receive_lora_uplink(request: Request):
    """
    Si on utilise The Things Network, configure le Webhook vers cette URL.
    Les données arrivent souvent en JSON complet.
    """
    if CURRENT_MODE != MODE_LORA:
        raise HTTPException(status_code=400, detail="Server in WiFi mode")
    
    # Exemple de récupération (à adapter selon le format Payload Formatter de TTN)
    payload = await request.json()
    print("Reçu LoRa:", payload)
    
    # TODO: Décoder le payload hexadécimal ici et mettre à jour 'current_wifi_buffer'
    # Simulation pour l'exemple :
    # update_buffer_from_hex(payload['uplink_message']['frm_payload'])
    
    return {"status": "received"}

# --- Route Calcul & Affichage ---
@app.get("/api/get_position")
async def get_position_api():
    """
    Appelé périodiquement par la page Web.
    1. Vérifie si des données récentes sont là.
    2. Lance le calcul.
    3. Met à jour l'historique.
    4. Renvoie le tout au navigateur.
    """
    # Timeout : Si pas de données depuis 10s, on est hors ligne
    if time.time() - last_buffer_update > 10.0:
        return {"status": "offline"}

    # Calcul de la position
    estimated_pos = algorithm_wknn(current_wifi_buffer)

    if estimated_pos:
        # Ajout à l'historique (pour tracer le chemin)
        # On évite les doublons si la position n'a pas changé depuis la dernière requête
        if not position_history or (position_history[-1]['timestamp'] != estimated_pos['timestamp']):
             # On peut aussi filtrer si la position est strictement identique pour économiser la mémoire
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