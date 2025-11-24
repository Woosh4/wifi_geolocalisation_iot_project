import uvicorn
from fastapi import FastAPI, HTTPException, Request
from fastapi.templating import Jinja2Templates # Import Templates
from fastapi.responses import HTMLResponse
from pydantic import BaseModel
import csv
import os
from datetime import datetime
from collections import deque # Efficient list for limited history

# === CONFIGURATION ===
HOST_IP = "172.20.10.8"
PORT = 5000
CSV_FILE = "wifi_data.csv"

# === DATA STORAGE ===
# We store the last 50 records in memory for the web dashboard
# This prevents the list from growing infinitely and crashing RAM
data_history = deque(maxlen=50)

# === DATA MODEL ===
class WifiScanData(BaseModel):
    timestamp: int
    mac_ap: str
    rssi: int
    ssid_ap: str

# === APP SETUP ===
app = FastAPI(title="ESP32 WiFi Tracker")
templates = Jinja2Templates(directory="templates") # Tell FastAPI where HTML is

def init_csv():
    if not os.path.exists(CSV_FILE):
        with open(CSV_FILE, mode='w', newline='') as file:
            writer = csv.writer(file)
            writer.writerow(["server_time", "esp_timestamp", "ssid", "mac", "rssi"])

@app.on_event("startup")
async def startup_event():
    init_csv()
    print(f"Server started. Dashboard available at http://localhost:{PORT}")

# === ENDPOINTS ===

# 1. WEB DASHBOARD (GET)
@app.get("/", response_class=HTMLResponse)
async def read_dashboard(request: Request):
    """
    Renders the HTML page using Jinja2.
    We pass the 'request' and the 'scans' list to the HTML.
    """
    return templates.TemplateResponse("index.html", {
        "request": request, 
        "scans": list(data_history) # Convert deque to list for HTML
    })

# 2. DATA RECEIVER (POST)
@app.post("/api/geoloc")
async def receive_wifi_data(data: WifiScanData):
    try:
        current_time = datetime.now().strftime('%H:%M:%S')
        
        print(f"[{current_time}] Received: {data.ssid_ap} | {data.rssi}dBm")

        # A. Save to CSV (Permanent Storage)
        with open(CSV_FILE, mode='a', newline='') as file:
            writer = csv.writer(file)
            writer.writerow([
                datetime.now().isoformat(),
                data.timestamp,
                data.ssid_ap,
                data.mac_ap,
                data.rssi
            ])

        # B. Add to In-Memory History (For Web Display)
        # We prepend (add to left) so the newest is at the top
        data_history.appendleft({
            "server_time": current_time,
            "ssid": data.ssid_ap,
            "mac": data.mac_ap,
            "rssi": data.rssi
        })

        return {"status": "success"}

    except Exception as e:
        print(f"Error: {e}")
        raise HTTPException(status_code=500, detail="Internal Server Error")

# === RUNNER ===
if __name__ == "__main__":
    uvicorn.run("server:app", host=HOST_IP, port=PORT, reload=True)