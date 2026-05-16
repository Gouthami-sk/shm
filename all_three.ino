#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "HX711.h"
#include <DHT.h>

// --- WiFi Credentials ---
const char* ssid = "StarLight"; 
const char* password = "Gouthami@1062";

// --- Pin Definitions ---
#define DHTPIN 5     // D1
#define VIB_PIN 14   // D5
#define DT_PIN 12    // D6
#define SCK_PIN 13   // D7

#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
HX711 scale;
ESP8266WebServer server(80);

// --- Variables ---
float calibration_factor = -7050.0; 
float currentForce = 0.0;
int currentVib = 0;
float currentTemp = 0.0;
float currentHum = 0.0;

// --- Web Dashboard HTML ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Structural Dashboard</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: sans-serif; background: #121212; color: white; text-align: center; padding: 10px; margin: 0; }
        .card { background: #1e1e1e; padding: 15px; border-radius: 12px; margin-bottom: 15px; border: 1px solid #333; position: relative; }
        canvas { background: #000; width: 100%; height: 160px; border-radius: 5px; border: 1px solid #444; display: block; }
        .scale-label { position: absolute; left: 15px; font-size: 0.6rem; color: #666; pointer-events: none; }
        .legend { display: flex; justify-content: center; gap: 15px; font-size: 0.9rem; margin-bottom: 8px; }
        #vib-status { padding: 12px; border-radius: 8px; font-weight: bold; background: #2d2d2d; }
        .alert { background: #ff4444 !important; box-shadow: 0 0 15px #ff4444; color: white; }
        .f-text { font-size: 1.3rem; color: #00d4ff; margin-bottom: 8px; }
    </style>
</head>
<body>
    <h3>Structural Health Monitor</h3>
    <div class="card">
        <div class="f-text">Force: <span id="f-num">0.00</span> N</div>
        <div class="scale-label" style="top: 45px;">+50</div>
        <div class="scale-label" style="top: 105px; color:#00d4ff;">0</div>
        <div class="scale-label" style="top: 165px;">-50</div>
        <canvas id="fCanvas"></canvas>
    </div>
    <div class="card">
        <div class="legend">
            <span style="color:#ff9800;">Temp: <span id="t-num">0</span>&deg;C</span>
            <span style="color:#4caf50;">Hum: <span id="h-num">0</span>%</span>
        </div>
        <canvas id="eCanvas"></canvas>
    </div>
    <div class="card"><div id="vib-status">STABLE</div></div>
    <script>
        const fCan = document.getElementById('fCanvas');
        const eCan = document.getElementById('eCanvas');
        const fCtx = fCan.getContext('2d');
        const eCtx = eCan.getContext('2d');
        let fHist = new Array(50).fill(0);
        let tHist = new Array(50).fill(0);
        let hHist = new Array(50).fill(0);

        function draw() {
            [fCan, eCan].forEach(c => { c.width = c.offsetWidth; c.height = c.offsetHeight; });
            let mid = fCan.height / 2;
            fCtx.clearRect(0,0,fCan.width,fCan.height);
            fCtx.strokeStyle='#333'; fCtx.beginPath(); fCtx.moveTo(0,mid); fCtx.lineTo(fCan.width,mid); fCtx.stroke();
            fCtx.strokeStyle='#ffff00'; fCtx.lineWidth=2; fCtx.beginPath();
            fHist.forEach((v,i) => {
                let x=(i/49)*fCan.width; let y=mid-(v/50*(fCan.height/2));
                if(i==0) fCtx.moveTo(x,y); else fCtx.lineTo(x,y);
            });
            fCtx.stroke();
            eCtx.clearRect(0,0,eCan.width,eCan.height);
            eCtx.lineWidth=2;
            eCtx.strokeStyle='#ff9800'; eCtx.beginPath();
            tHist.forEach((v,i) => {
                let x=(i/49)*eCan.width; let y=eCan.height-(v/100*eCan.height);
                if(i==0) eCtx.moveTo(x,y); else eCtx.lineTo(x,y);
            });
            eCtx.stroke();
            eCtx.strokeStyle='#4caf50'; eCtx.beginPath();
            hHist.forEach((v,i) => {
                let x=(i/49)*eCan.width; let y=eCan.height-(v/100*eCan.height);
                if(i==0) eCtx.moveTo(x,y); else eCtx.lineTo(x,y);
            });
            eCtx.stroke();
        }

        setInterval(() => {
            fetch('/data').then(res => res.text()).then(raw => {
                let d = raw.split(',');
                document.getElementById('f-num').innerText = d[0];
                document.getElementById('t-num').innerText = d[2];
                document.getElementById('h-num').innerText = d[3];
                fHist.push(parseFloat(d[0])); fHist.shift();
                tHist.push(parseFloat(d[2])); tHist.shift();
                hHist.push(parseFloat(d[3])); hHist.shift();
                draw();
                let v = document.getElementById('vib-status');
                if(d[1] == "1") { v.innerText="VIBRATION DETECTED!"; v.className="alert"; }
                else { v.innerText="STABLE"; v.className=""; }
            });
        }, 1000);
    </script>
</body>
</html>
)rawliteral";

void handleData() {
    String p = String(currentForce, 2) + "," + String(currentVib) + "," + String(currentTemp, 1) + "," + String(currentHum, 1);
    server.send(200, "text/plain", p);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    dht.begin();
    pinMode(VIB_PIN, INPUT);
    scale.begin(DT_PIN, SCK_PIN);
    scale.set_scale(calibration_factor);
    scale.tare();

    WiFi.begin(ssid, password);
    Serial.print("\nConnecting WiFi");
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }

    Serial.println("\n--- SYSTEM READY ---");
    Serial.print("IP: http://"); Serial.println(WiFi.localIP());
    Serial.println("---------------------------------------------------------");
    Serial.println("Force(N) | Vib | Temp(C) | Hum(%) | IP Address");
    Serial.println("---------------------------------------------------------");

    server.on("/", []() { server.send_P(200, "text/html", index_html); });
    server.on("/data", handleData);
    server.begin();
}

void loop() {
    server.handleClient();
    
    // Read Sensors
    if (scale.is_ready()) {
        float raw = scale.get_units(3);
        currentForce = (raw * 0.4) + (currentForce * 0.6);
        if (abs(currentForce) < 0.15) currentForce = 0.00;
    }
    currentVib = digitalRead(VIB_PIN);
    
    static unsigned long lastDHT = 0;
    if (millis() - lastDHT > 2000) {
        float t = dht.readTemperature();
        float h = dht.readHumidity();
        if (!isnan(t) && !isnan(h)) { currentTemp = t; currentHum = h; }
        lastDHT = millis();
    }

    // --- Print Sensor Table to Serial Monitor ---
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 2000) {
        Serial.print(currentForce, 2); Serial.print(" N  | ");
        Serial.print(currentVib == 1 ? "ON " : "OFF"); Serial.print(" | ");
        Serial.print(currentTemp, 1); Serial.print(" C   | ");
        Serial.print(currentHum, 1); Serial.print(" %   | ");
        Serial.println(WiFi.localIP());
        lastPrint = millis();
    }
}