import uvicorn
from fastapi import FastAPI, Request
from fastapi.templating import Jinja2Templates
from fastapi.responses import HTMLResponse
from pydantic import BaseModel
import json
import os
import math
import time
from collections import defaultdict

# === CONFIGURATION ===
HOST_IP = "0.0.0.0"
PORT = 5000
DB_FILE = "database_wifi.json"
K_NEIGHBORS = 3  # Nombre de voisins pour le k-NN

app = FastAPI()
templates = Jinja2Templates(directory="templates")

# === MÉMOIRE ===
# "fingerprint_db" sera une liste de dict : [{'lat':..., 'lon':..., 'floor':..., 'aps': {mac: rssi, ...}}, ...]
fingerprint_db = []
# Buffer pour recevoir les données temps réel de l'ESP32 (qui arrivent paquet par paquet)
current_live_scan = {} 
last_scan_update = 0

class WifiData(BaseModel):
    timestamp: int
    ssid: str
    mac: str
    rssi: int

# === CHARGEMENT ET PRÉPARATION DE LA BDD ===
def load_and_structure_db():
    global fingerprint_db
    if not os.path.exists(DB_FILE):
        print("Aucune base de données trouvée !")
        return

    with open(DB_FILE, 'r') as f:
        raw_data = json.load(f)

    # L'étape cruciale : Regrouper les lignes CSV/JSON par Timestamp
    # Un timestamp unique = Une position physique (Fingerprint)
    grouped_scans = defaultdict(lambda: {'lat': 0, 'lon': 0, 'floor': 0, 'aps': {}})
    
    for entry in raw_data:
        ts = entry['timestamp']
        # On remplit les infos de position (supposées identiques pour un même timestamp)
        grouped_scans[ts]['lat'] = entry['latitude']
        grouped_scans[ts]['lon'] = entry['longitude']
        grouped_scans[ts]['floor'] = entry['floor']
        # On ajoute le réseau au dictionnaire de cette scène
        grouped_scans[ts]['aps'][entry['mac']] = entry['rssi']

    # Conversion en liste propre pour l'algo
    fingerprint_db = list(grouped_scans.values())
    print(f"Base chargée : {len(fingerprint_db)} empreintes de référence.")

# === ALGORITHME DE LOCALISATION (WKNN) ===
def calculate_position(live_aps):
    """
    Compare 'live_aps' (dict {mac: rssi}) avec 'fingerprint_db'.
    Retourne {lat, lon, floor, accuracy}
    """
    if not fingerprint_db or not live_aps:
        return None

    distances = []

    # 1. Calculer la "distance signal" avec chaque empreinte de la base
    for fp in fingerprint_db:
        dist_sq_sum = 0
        common_count = 0
        
        # On compare uniquement les MACs en commun
        for mac, rssi_live in live_aps.items():
            if mac in fp['aps']:
                rssi_db = fp['aps'][mac]
                dist_sq_sum += (rssi_live - rssi_db) ** 2
                common_count += 1
            else:
                # Pénalité si le MAC est vu en live mais pas dans la base
                dist_sq_sum += (100) ** 2 

        # Si aucun point commun, distance infinie
        if common_count == 0:
            final_dist = 9999999
        else:
            final_dist = math.sqrt(dist_sq_sum)
        
        distances.append({
            "dist": final_dist,
            "lat": fp['lat'],
            "lon": fp['lon'],
            "floor": fp['floor']
        })

    # 2. Trier par distance (le plus petit = le plus ressemblant)
    distances.sort(key=lambda x: x["dist"])

    # 3. Prendre les K meilleurs voisins
    k_nearest = distances[:K_NEIGHBORS]
    
    # 4. Moyenne pondérée (Weighted Average)
    weight_sum = 0
    lat_sum = 0
    lon_sum = 0
    floor_sum = 0
    
    # Calcul de l'incertitude (écart type des positions des voisins)
    coords_for_variance = []

    for item in k_nearest:
        # Poids inverse à la distance (plus c'est proche, plus c'est lourd)
        # On ajoute 0.1 pour éviter la division par zéro
        w = 1 / (item["dist"] + 0.1)
        
        lat_sum += item["lat"] * w
        lon_sum += item["lon"] * w
        floor_sum += item["floor"] * w
        weight_sum += w
        
        coords_for_variance.append((item["lat"], item["lon"]))

    if weight_sum == 0: return None

    est_lat = lat_sum / weight_sum
    est_lon = lon_sum / weight_sum
    est_floor = round(floor_sum / weight_sum) # Arrondi à l'étage le plus proche

    # Estimation de l'incertitude (en mètres approx)
    # Simple méthode : distance moyenne entre le point estimé et les voisins
    uncertainty_score = 0
    for lat, lon in coords_for_variance:
        # Distance euclidienne simple sur les coord (très approximatif pour lat/lon mais suffisant pour du debug)
        d = math.sqrt((lat - est_lat)**2 + (lon - est_lon)**2)
        uncertainty_score += d
    
    # Conversion degrés -> mètres (approx à Paris)
    accuracy_meters = (uncertainty_score / K_NEIGHBORS) * 111000 

    return {
        "lat": est_lat,
        "lon": est_lon,
        "floor": est_floor,
        "accuracy": accuracy_meters,
        "neighbors_dist": [round(x['dist'],1) for x in k_nearest] # pour debug
    }

# === ROUTES ===

@app.on_event("startup")
async def startup_event():
    load_and_structure_db()

@app.get("/", response_class=HTMLResponse)
async def map_page(request: Request):
    return templates.TemplateResponse("map.html", {"request": request})

@app.post("/api/raw_scan") # Même endpoint que l'ESP32 utilise
async def receive_live_data(data: WifiData):
    global current_live_scan, last_scan_update
    
    # Si le dernier paquet date de plus de 2 secondes, on considère que c'est un nouveau scan complet
    if time.time() - last_scan_update > 2.0:
        current_live_scan = {} # Reset buffer
        
    current_live_scan[data.mac] = data.rssi
    last_scan_update = time.time()
    
    # print(f"Recu: {data.ssid} ({len(current_live_scan)} APs dans le buffer)")
    return {"status": "ok"}

@app.get("/api/get_position")
async def get_position_json():
    """Appelé par la page web pour mettre à jour la carte"""
    # Si on n'a pas reçu de données depuis 10 secondes, on dit que c'est hors ligne
    if time.time() - last_scan_update > 10.0:
        return {"status": "offline"}
        
    position = calculate_position(current_live_scan)
    
    if position:
        return {"status": "tracking", "data": position}
    else:
        return {"status": "calibrating"}

if __name__ == "__main__":
    uvicorn.run("server_geoloc:app", host=HOST_IP, port=PORT, reload=True)