# wifi_geolocalisation_iot_projet
This is an IoT projet where the goal is to scan nearby wifis using an esp32, send these wifis to a server using TTN gateways, and the server estimates the position of the esp using its database.

Files for building the database are : in capture_wifi_dataset for the esp32, and server_wifi_capture (was used as a local server).

Files for using the project are : the file in scan for the esp32, server_geoloc.py for the server and database_wifi_cclean.json for the database.

The raw database is named in database_wifi.json
The cleaned up database (all the wifi sharing from phones) is named database_wifi_clean.json
