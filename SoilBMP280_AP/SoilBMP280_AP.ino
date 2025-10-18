#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>

const char* ssid = "nex_1";
const char* password = "";

const byte DNS_PORT = 53;
DNSServer dnsServer;

#define PROBE_POWER_PIN D5
#define SDA_PIN D2
#define SCL_PIN D1

#define DRY_VALUE 750
#define WET_VALUE 300
#define NUM_SAMPLES 30
#define READ_INTERVAL_MS 5000UL

Adafruit_BMP280 bmp;
bool bmp_ok = false;

ESP8266WebServer server(80);

int moisturePercent = 0;
int moistureRaw = 0;
float temperature = NAN;
float pressure = NAN;
unsigned long lastReadTime = 0;

void takeSensorReadings();

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("Starting ESP8266 Soil + BMP280 Monitor");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  dnsServer.start(DNS_PORT, "*", apIP);

  pinMode(PROBE_POWER_PIN, OUTPUT);
  digitalWrite(PROBE_POWER_PIN, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  bmp_ok = bmp.begin(0x76);
  if (bmp_ok) {
    bmp.setSampling(
      Adafruit_BMP280::MODE_NORMAL,
      Adafruit_BMP280::SAMPLING_X2,
      Adafruit_BMP280::SAMPLING_X16,
      Adafruit_BMP280::FILTER_X16,
      Adafruit_BMP280::STANDBY_MS_63
    );
  }

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.onNotFound(handleRoot);
  server.begin();

  takeSensorReadings();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  unsigned long now = millis();
  if (now - lastReadTime >= READ_INTERVAL_MS) {
    takeSensorReadings();
  }
}

void takeSensorReadings() {
  lastReadTime = millis();

  digitalWrite(PROBE_POWER_PIN, HIGH);
  delay(150);

  uint32_t total = 0;
  for (int i = 0; i < NUM_SAMPLES; i++) {
    total += analogRead(A0);
    delay(4);
    yield();
  }
  digitalWrite(PROBE_POWER_PIN, LOW);

  int avg = total / NUM_SAMPLES;
  moistureRaw = avg;
  moisturePercent = map(avg, WET_VALUE, DRY_VALUE, 100, 0);
  moisturePercent = constrain(moisturePercent, 0, 100);

  if (bmp_ok) {
    temperature = bmp.readTemperature();
    pressure = bmp.readPressure() / 100.0F;
  } else {
    temperature = NAN;
    pressure = NAN;
  }

  Serial.printf("Raw:%d Moist:%d%% Temp:%.2fC Pres:%.2fhPa\n",
                avg, moisturePercent, temperature, pressure);
}

void handleRoot() {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <title>Soil & BMP280 Monitor</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body { font-family: Arial, sans-serif; background: #f4f9ff; color:#333; text-align:center; padding: 30px; }
      .card { background:#fff; border-radius:12px; padding:25px; max-width:420px; margin:auto; box-shadow:0 6px 15px rgba(0,0,0,0.1); }
      h1 { color:#2e7d32; }
      .reading { font-size:42px; color:#1565c0; }
      .label { margin-top:12px; font-size:18px; color:#444; }
      .tiny { font-size: 12px; color: #666; }
      .footer { margin-top: 24px; font-size: 13px; color: #777; }
    </style>
  </head>
  <body>
    <div class="card">
      <h1>🌱 Soil & Air Monitor</h1>

      <div class="label">Soil Moisture</div>
      <div class="reading" id="moisture">--%</div>
      <div class="tiny" id="raw">raw: --</div>

      <div class="label">Temperature</div>
      <div class="reading" id="temp">--°C</div>

      <div class="label">Pressure</div>
      <div class="reading" id="pres">-- hPa</div>

      <div class="tiny" id="ts">Last update: --</div>
    </div>
    <div class="footer">nexus</div>

    <script>
      async function updateData() {
        try {
          const res = await fetch('/data', { cache: 'no-store' });
          const data = await res.json();
          document.getElementById('moisture').textContent = data.moisture + '%';
          document.getElementById('raw').textContent = 'raw: ' + data.moisture_raw;
          document.getElementById('temp').textContent = isFinite(data.temperature) ? data.temperature.toFixed(1) + '°C' : '--';
          document.getElementById('pres').textContent = isFinite(data.pressure) ? data.pressure.toFixed(1) + ' hPa' : '--';
          document.getElementById('ts').textContent = 'Last update: ' + new Date(data.timestamp).toLocaleTimeString();
        } catch (err) {
          console.error('Update failed', err);
        }
      }
      updateData();
      setInterval(updateData, 5000);
    </script>
  </body>
  </html>
  )rawliteral";

  server.send(200, "text/html", html);
}

void handleData() {
  if (millis() - lastReadTime > 1000UL) {
    takeSensorReadings();
  }
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");

  String json = "{";
  json += "\"moisture\":" + String(moisturePercent) + ",";
  json += "\"moisture_raw\":" + String(moistureRaw) + ",";
  json += "\"temperature\":" + String(temperature, 2) + ",";
  json += "\"pressure\":" + String(pressure, 2) + ",";
  json += "\"timestamp\":" + String((unsigned long)(millis()));
  json += "}";
  server.send(200, "application/json", json);
}
