#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// ================= WIFI =================
const char* ssid     = "DaveFonyod";
const char* password = "3910020245750005";

// ================= RS485 =================
#define RS485_RX 20
#define RS485_TX 21
#define RS485_DE 6

HardwareSerial BMS(1);
WebServer server(80);

// ================= DATA =================
float cells[17];
float tempC[3];
float packVoltage = 0;
unsigned long lastUpdate = 0;

// ================= UTILS =================
void flush_rx() {
  while (BMS.available()) BMS.read();
}

// ================= SEND COMMANDS =================
void rs485_send(const uint8_t* cmd, size_t len) {
  digitalWrite(RS485_DE, HIGH);
  delayMicroseconds(200);
  BMS.write(cmd, len);
  BMS.flush();
  delayMicroseconds(200);
  digitalWrite(RS485_DE, LOW);
}

void send_cmd_cells() {
  const uint8_t cmd[] = {
    0xFE,0xFE,0xFE,0xFE,
    0x68,0x31,0xCE,0x68,
    0x02,0x02,
    0x6F,0x5B,0x9D,
    0x16
  };
  rs485_send(cmd, sizeof(cmd));
}

void send_cmd_pack() {
  const uint8_t cmd[] = {
    0xFE,0xFE,0xFE,0xFE,
    0x68,0x31,0xCE,0x68,
    0x02,0x02,
    0x60,0x42,0x75,
    0x16
  };
  rs485_send(cmd, sizeof(cmd));
}

// ================= READ & PARSE =================
bool read_frames() {
  uint8_t buf[220];
  int len = 0;
  unsigned long t0 = millis();
 
  while (millis() - t0 < 400) {
    while (BMS.available() && len < (int)sizeof(buf)) {
      buf[len++] = BMS.read();
    }
  }
   Serial.print("RX bytes: ");
  Serial.println(len);
  bool got_cells = false;
  bool got_pack  = false;

  for (int i = 0; i <= len - 6; i++) {
    if (buf[i]==0x68 && buf[i+1]==0x31 && buf[i+2]==0xCE &&
        buf[i+3]==0x68 && buf[i+4]==0x82) {

      uint8_t plen = buf[i+5];
      int p = i + 6;

      // ---------- CELLS ----------
      if (plen == 0x28 && p + plen + 1 < len && buf[p+plen+1] == 0x16) {
        for (int c=0; c<17; c++) {
          uint16_t raw = (buf[p + c*2] << 8) | buf[p + c*2 + 1];
          cells[c] = (raw - 0x3333) / 1000.0;
        }
        got_cells = true;
      }

      // ---------- PACK ----------
      if (plen == 0x0F && p + plen + 1 < len && buf[p+plen+1] == 0x16) {
        uint16_t rawV = (buf[p] << 8) | buf[p+1];
        packVoltage = (rawV - 0x3333) / 10.0;

        tempC[0] = buf[p+11] - 0x33;
        tempC[1] = buf[p+12] - 0x33;
        tempC[2] = buf[p+13] - 0x33;

        got_pack = true;
      }
    }
  }

  return got_cells && got_pack;
}

// ================= WEB =================
void handle_root() {
  String html;
  html += "<html><head><meta name='viewport' content='width=device-width'>";
  html += "<style>body{background:#111;color:#eee;font-family:Arial}</style></head><body>";
  html += "<h2>NIU BMS (ESP32-C3)</h2>";
  html += "<p><b>Pack Voltage:</b> " + String(packVoltage,1) + " V</p>";

  html += "<h3>Cells</h3><ul>";
  for (int i=0;i<17;i++) {
    html += "<li>Cell " + String(i+1) + ": " + String(cells[i],3) + " V</li>";
  }
  html += "</ul>";

  html += "<h3>Temperatures</h3>";
  html += "T1: " + String(tempC[0],1) + " C<br>";
  html += "T2: " + String(tempC[1],1) + " C<br>";
  html += "T3: " + String(tempC[2],1) + " C<br>";

  html += "<p><small>Updated ";
  html += String((millis() - lastUpdate)/1000);
  html += "s ago</small></p>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handle_api() {
  String json = "{";
  json += "\"packVoltage\":" + String(packVoltage,1) + ",";
  json += "\"temps\":[" + String(tempC[0],1) + "," + String(tempC[1],1) + "," + String(tempC[2],1) + "],";
  json += "\"cells\":[";
  for (int i=0;i<17;i++) {
    json += String(cells[i],3);
    if (i < 16) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

// ================= SETUP =================
void setup() {
  pinMode(RS485_DE, OUTPUT);
  digitalWrite(RS485_DE, LOW);

  Serial.begin(115200);
  BMS.begin(9600, SERIAL_8E1, RS485_RX, RS485_TX);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(300);

  server.on("/", handle_root);
  server.on("/api/bms", handle_api);
  server.begin();

  Serial.print("ESP32-C3 ready: ");
  Serial.println(WiFi.localIP());
}

// ================= LOOP =================
void loop() {
  server.handleClient();

  if (millis() - lastUpdate > 3000) {
    flush_rx();
    send_cmd_pack();
    delay(120);
    send_cmd_cells();
    delay(200);
    if (read_frames()) lastUpdate = millis();
  }
}