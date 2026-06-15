/*
 * ESP32-Based IoT Smart Electricity Monitoring & Alert System
 * Components: ESP32 + ACS712 (30A) + 220V AC Bulb
 * Features: Real-time current, power, kWh, per-day limit alert
 */

#include <WiFi.h>
#include <WebServer.h>
#include <math.h>

// ========== WiFi Credentials ==========
const char* ssid     = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

// ========== ACS712 Config ==========
#define ACS_PIN        34       // Analog pin connected to ACS712 output
#define ACS_SENSITIVITY 0.066   // 30A model = 66mV/A
#define VRMS_SUPPLY    220.0    // India = 220V AC
#define SAMPLES        1000     // Samples per RMS calculation
#define ADC_REF        3.3      // ESP32 ADC reference voltage
#define ADC_RESOLUTION 4095.0   // 12-bit ADC

// ========== Electricity Rate ==========
float ratePerUnit = 6.5;  // Rs per kWh (Tamil Nadu EB rate approx)

// ========== Global Variables ==========
float currentRMS    = 0.0;
float powerWatts    = 0.0;
float energyKWh     = 0.0;
float dailyLimitKWh = 1.0;
bool  limitExceeded = false;

unsigned long lastCalcTime = 0;
unsigned long dayStartTime = 0;

WebServer server(80);

// ========== Read ACS712 RMS Current ==========
float readCurrentRMS() {
  long sum = 0;
  int midpoint = 2048;

  for (int i = 0; i < SAMPLES; i++) {
    int raw = analogRead(ACS_PIN);
    int centered = raw - midpoint;
    sum += (long)centered * centered;
    delayMicroseconds(100);
  }

  float rms_raw = sqrt((float)sum / SAMPLES);
  float voltage_rms = rms_raw * (ADC_REF / ADC_RESOLUTION);
  float current = voltage_rms / ACS_SENSITIVITY;

  if (current < 0.05) current = 0.0;
  return current;
}

// ========== HTML Dashboard ==========
String getHTML() {
  float billEstimate = energyKWh * ratePerUnit;
  float percentUsed  = (dailyLimitKWh > 0) ? (energyKWh / dailyLimitKWh) * 100.0 : 0;
  if (percentUsed > 100) percentUsed = 100;

  String fillColor = limitExceeded ? "#ef4444" : (percentUsed > 75 ? "#f59e0b" : "#00d4aa");

  String limitStatus = limitExceeded
    ? "<div class='alert'>⚠️ LIMIT EXCEEDED! Daily limit of " + String(dailyLimitKWh, 2) + " kWh reached!</div>"
    : "<div class='safe'>✅ Within daily limit</div>";

  String html = "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>";
  html += "<meta http-equiv='refresh' content='3'>";
  html += "<title>Smart Electricity Monitor</title>";
  html += "<style>";
  html += "@import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;600;700&family=Space+Mono:wght@400;700&display=swap');";
  html += ":root{--bg:#0a0e1a;--card:#111827;--border:#1e2d45;--accent:#00d4aa;--accent2:#3b82f6;--danger:#ef4444;--warning:#f59e0b;--text:#e2e8f0;--muted:#64748b;}";
  html += "*{margin:0;padding:0;box-sizing:border-box;}";
  html += "body{background:var(--bg);color:var(--text);font-family:'Inter',sans-serif;min-height:100vh;padding:20px;}";
  html += ".header{text-align:center;padding:30px 0 20px;border-bottom:1px solid var(--border);margin-bottom:30px;}";
  html += ".header h1{font-family:'Space Mono',monospace;font-size:1.4rem;color:var(--accent);letter-spacing:2px;text-transform:uppercase;}";
  html += ".header p{color:var(--muted);font-size:0.8rem;margin-top:6px;}";
  html += ".live-dot{display:inline-block;width:8px;height:8px;background:var(--accent);border-radius:50%;margin-right:6px;animation:pulse 1.5s infinite;}";
  html += "@keyframes pulse{0%,100%{opacity:1;transform:scale(1);}50%{opacity:0.4;transform:scale(0.8);}}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:16px;margin-bottom:24px;}";
  html += ".card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:24px 20px;text-align:center;}";
  html += ".card .label{font-size:0.7rem;color:var(--muted);text-transform:uppercase;letter-spacing:1.5px;margin-bottom:12px;}";
  html += ".card .value{font-family:'Space Mono',monospace;font-size:2.2rem;font-weight:700;color:var(--accent);line-height:1;}";
  html += ".card .unit{font-size:0.85rem;color:var(--muted);margin-top:6px;}";
  html += ".card.blue .value{color:var(--accent2);}.card.amber .value{color:var(--warning);}.card.green .value{color:#22c55e;}";
  html += ".alert{background:rgba(239,68,68,0.12);border:1px solid var(--danger);color:var(--danger);border-radius:10px;padding:14px 20px;text-align:center;font-weight:600;margin-bottom:20px;}";
  html += ".safe{background:rgba(34,197,94,0.1);border:1px solid #22c55e;color:#22c55e;border-radius:10px;padding:14px 20px;text-align:center;font-weight:600;margin-bottom:20px;}";
  html += ".progress-section{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:20px;margin-bottom:20px;}";
  html += ".progress-label{display:flex;justify-content:space-between;font-size:0.8rem;color:var(--muted);margin-bottom:10px;}";
  html += ".progress-bar{background:var(--border);border-radius:99px;height:10px;overflow:hidden;}";
  html += ".progress-fill{height:100%;border-radius:99px;}";
  html += ".settings-card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:24px;margin-bottom:20px;}";
  html += ".settings-card h3{font-size:0.8rem;color:var(--muted);text-transform:uppercase;letter-spacing:1.5px;margin-bottom:16px;}";
  html += ".input-row{display:flex;gap:10px;align-items:center;flex-wrap:wrap;}";
  html += ".input-row input{background:var(--bg);border:1px solid var(--border);color:var(--text);padding:10px 14px;border-radius:8px;font-size:0.95rem;font-family:'Space Mono',monospace;width:130px;}";
  html += ".input-row input:focus{outline:none;border-color:var(--accent);}";
  html += ".btn{background:var(--accent);color:#0a0e1a;border:none;padding:10px 20px;border-radius:8px;font-weight:700;font-size:0.85rem;cursor:pointer;}";
  html += ".btn:hover{opacity:0.85;}.btn-danger{background:transparent;color:var(--danger);border:1px solid var(--danger);}";
  html += ".footer{text-align:center;color:var(--muted);font-size:0.75rem;margin-top:20px;padding-top:20px;border-top:1px solid var(--border);}";
  html += "</style></head><body>";

  html += "<div class='header'><h1><span class='live-dot'></span>Smart Electricity Monitor</h1><p>ESP32 + ACS712 | Auto-refresh every 3 seconds</p></div>";

  html += limitStatus;

  html += "<div class='grid'>";
  html += "<div class='card'><div class='label'>Current</div><div class='value'>" + String(currentRMS, 2) + "</div><div class='unit'>Amperes (A)</div></div>";
  html += "<div class='card blue'><div class='label'>Power</div><div class='value'>" + String(powerWatts, 1) + "</div><div class='unit'>Watts (W)</div></div>";
  html += "<div class='card amber'><div class='label'>Today's Usage</div><div class='value'>" + String(energyKWh, 4) + "</div><div class='unit'>Units (kWh)</div></div>";
  html += "<div class='card green'><div class='label'>Bill Estimate</div><div class='value'>Rs." + String(billEstimate, 2) + "</div><div class='unit'>Today approx</div></div>";
  html += "</div>";

  html += "<div class='progress-section'>";
  html += "<div class='progress-label'><span>Daily Usage</span><span>" + String(percentUsed, 1) + "% of " + String(dailyLimitKWh, 2) + " kWh</span></div>";
  html += "<div class='progress-bar'><div class='progress-fill' style='width:" + String(percentUsed) + "%;background:" + fillColor + "'></div></div>";
  html += "</div>";

  html += "<div class='settings-card'><h3>Set Daily Limit</h3><div class='input-row'>";
  html += "<input type='number' id='limitInput' step='0.1' min='0.1' placeholder='e.g. 1.5' />";
  html += "<button class='btn' onclick='setLimit()'>Set Limit</button>";
  html += "<button class='btn btn-danger' onclick='resetEnergy()'>Reset Today</button>";
  html += "</div></div>";

  html += "<div class='settings-card'><h3>Electricity Rate (Rs/unit)</h3><div class='input-row'>";
  html += "<input type='number' id='rateInput' step='0.1' min='1' placeholder='e.g. 6.5' />";
  html += "<button class='btn' onclick='setRate()'>Update Rate</button>";
  html += "</div></div>";

  html += "<div class='footer'>ESP32-Based IoT Smart Electricity Monitoring System</div>";

  html += "<script>";
  html += "function setLimit(){var v=document.getElementById('limitInput').value;if(!v||v<=0){alert('Enter valid limit!');return;}fetch('/setlimit?val='+v).then(()=>location.reload());}";
  html += "function resetEnergy(){if(confirm('Reset today energy to 0?'))fetch('/reset').then(()=>location.reload());}";
  html += "function setRate(){var v=document.getElementById('rateInput').value;if(!v||v<=0){alert('Enter valid rate!');return;}fetch('/setrate?val='+v).then(()=>location.reload());}";
  html += "</script></body></html>";

  return html;
}

// ========== Web Handlers ==========
void handleRoot()     { server.send(200, "text/html", getHTML()); }
void handleSetLimit() { if(server.hasArg("val")) dailyLimitKWh=server.arg("val").toFloat(); server.sendHeader("Location","/"); server.send(302,"text/plain",""); }
void handleReset()    { energyKWh=0.0; dayStartTime=millis(); limitExceeded=false; server.sendHeader("Location","/"); server.send(302,"text/plain",""); }
void handleSetRate()  { if(server.hasArg("val")) ratePerUnit=server.arg("val").toFloat(); server.sendHeader("Location","/"); server.send(302,"text/plain",""); }

// ========== Setup ==========
void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
  Serial.println("Open browser and go to: http://" + WiFi.localIP().toString());

  server.on("/",         handleRoot);
  server.on("/setlimit", handleSetLimit);
  server.on("/reset",    handleReset);
  server.on("/setrate",  handleSetRate);
  server.begin();

  dayStartTime = lastCalcTime = millis();
}

// ========== Loop ==========
void loop() {
  server.handleClient();

  unsigned long now = millis();

  if (now - lastCalcTime >= 1000) {
    float elapsed   = (now - lastCalcTime) / 1000.0;
    currentRMS      = readCurrentRMS();
    powerWatts      = currentRMS * VRMS_SUPPLY;
    energyKWh      += (powerWatts * (elapsed / 3600.0)) / 1000.0;
    limitExceeded   = (energyKWh >= dailyLimitKWh);
    lastCalcTime    = now;

    Serial.printf("I=%.2fA | P=%.1fW | E=%.4fkWh | %s\n",
      currentRMS, powerWatts, energyKWh, limitExceeded ? "EXCEEDED!" : "OK");
  }

  // Auto reset every 24 hours
  if (now - dayStartTime >= 86400000UL) {
    energyKWh = 0.0; dayStartTime = now; limitExceeded = false;
    Serial.println("Midnight reset done!");
  }
}
