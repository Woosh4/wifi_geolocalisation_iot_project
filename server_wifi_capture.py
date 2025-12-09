import uvicorn
from fastapi import FastAPI, Request, Form
from fastapi.templating import Jinja2Templates
from fastapi.responses import HTMLResponse, RedirectResponse
from pydantic import BaseModel
import json
import os
from datetime import datetime

# === CONFIGURATION ===
HOST_IP = "0.0.0.0"
PORT = 5000
DB_FILE = "database_wifi.json" # On garde le même fichier de sauvegarde

app = FastAPI()
# Dossier où se trouvent les fichiers HTML
templates = Jinja2Templates(directory="templates")

# === STOCKAGE EN MÉMOIRE (File d'attente) ===
pending_scans = {}

# === MODÈLES ===
class WifiData(BaseModel):
    timestamp: int
    ssid: str
    mac: str
    rssi: int

# === FONCTIONS ===
def save_to_json_db(data_list):
    current_db = []
    if os.path.exists(DB_FILE):
        try:
            with open(DB_FILE, 'r') as f:
                current_db = json.load(f)
        except:
            current_db = []
    
    current_db.extend(data_list)
    
    with open(DB_FILE, 'w') as f:
        json.dump(current_db, f, indent=4)

# === ROUTES ===

@app.get("/", response_class=HTMLResponse)
async def home(request: Request):
    # Tri des timestamps (le plus récent en haut)
    sorted_timestamps = sorted(pending_scans.keys(), reverse=True)
    
    scans_display = []
    for ts in sorted_timestamps:
        time_str = datetime.fromtimestamp(ts).strftime('%H:%M:%S')
        count = len(pending_scans[ts])
        scans_display.append({"ts": ts, "time_str": time_str, "count": count})

    # MODIFICATION ICI : On appelle le NOUVEAU fichier HTML
    return templates.TemplateResponse("index_wifi_capture.html", {
        "request": request, 
        "pending": scans_display
    })

@app.post("/api/raw_scan")
async def receive_raw(data: WifiData):
    ts = data.timestamp
    if ts not in pending_scans:
        pending_scans[ts] = []
    
    pending_scans[ts].append(data.dict())
    print(f"Reçu: {data.ssid} ({data.rssi}) - Attente validation web")
    return {"status": "stored_temporarily"}

@app.post("/tag_scan")
async def tag_scan(
    timestamp: int = Form(...), 
    lat: float = Form(...), 
    lon: float = Form(...), 
    floor: int = Form(...)
):
    if timestamp in pending_scans:
        wifi_list = pending_scans[timestamp]
        
        geolocated_data = []
        for wifi in wifi_list:
            enriched_wifi = wifi.copy()
            enriched_wifi["latitude"] = lat
            enriched_wifi["longitude"] = lon
            enriched_wifi["floor"] = floor
            geolocated_data.append(enriched_wifi)
        
        save_to_json_db(geolocated_data)
        
        del pending_scans[timestamp]
        print(f"Scan {timestamp} validé et sauvegardé !")
        
    # Redirection vers l'accueil après sauvegarde
    return RedirectResponse(url="/", status_code=303)

if __name__ == "__main__":
    uvicorn.run("server_wifi_capture:app", host=HOST_IP, port=PORT, reload=True)