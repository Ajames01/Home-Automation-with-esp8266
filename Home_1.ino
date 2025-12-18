#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

// OLED Display settings - FIXED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Soft AP settings
const char* ap_ssid = "HomeAutomation";
const char* ap_password = "12345678";

// Admin credentials
String admin_username = "admin";
String admin_password = "admin123";

// DNS and Web Server
const byte DNS_PORT = 53;
DNSServer dnsServer;
ESP8266WebServer server(80);

// Relay pins
const int RELAY1 = D1;  // GPIO5
const int RELAY2 = D2;  // GPIO4
const int RELAY3 = D5;  // GPIO14
const int RELAY4 = D6;  // GPIO12

// Relay states
bool relayStates[4] = {false, false, false, false};
String roomNames[4] = {"Living", "Bedroom", "Kitchen", "Bath"};

// System stats
int connectedClients = 0;
String sessionToken = "";
bool oledWorking = false;

// EEPROM
#define EEPROM_SIZE 64
#define ADDR_RELAY_STATES 0

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== HOME AUTOMATION STARTING ===");
  
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Initialize I2C with specific pins
  Wire.begin(D3, D4);  // SDA=D3(GPIO0), SCL=D4(GPIO2)
  Wire.setClock(100000); // Slow down I2C to 100kHz for stability
  delay(100);
  
  // Initialize OLED with error checking
  Serial.println("Initializing OLED...");
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("ERROR: OLED not found!");
    Serial.println("Check wiring: SDA->D3, SCL->D4, VCC->3.3V, GND->GND");
    oledWorking = false;
  } else {
    Serial.println("OLED initialized successfully!");
    oledWorking = true;
    
    // Clear and test display
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("HOME AUTOMATION");
    display.println("Starting...");
    display.display();
    delay(1000);
  }
  
  // Initialize relay pins
  Serial.println("Initializing relays...");
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);
  pinMode(RELAY4, OUTPUT);
  
  // Turn all relays OFF
  digitalWrite(RELAY1, LOW);
  digitalWrite(RELAY2, LOW);
  digitalWrite(RELAY3, LOW);
  digitalWrite(RELAY4, LOW);
  Serial.println("All relays OFF");
  
  // Load saved states
  loadRelayStates();
  
  // Setup WiFi AP
  Serial.println("Setting up Access Point...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP Started! IP: ");
  Serial.println(IP);
  
  // Setup DNS for captive portal
  dnsServer.start(DNS_PORT, "*", IP);
  Serial.println("DNS server started");
  
  // Setup web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/toggle", HTTP_GET, handleToggle);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/all", HTTP_GET, handleAll);
  server.on("/scene", HTTP_POST, handleScene);
  server.on("/generate_204", handleRoot);
  server.on("/fwlink", handleRoot);
  server.onNotFound(handleRoot);
  
  server.begin();
  Serial.println("HTTP server started");
  
  // Update display
  updateOLED();
  
  Serial.println("\n=== SYSTEM READY ===");
  Serial.println("Connect to WiFi: " + String(ap_ssid));
  Serial.println("Password: " + String(ap_password));
  Serial.println("IP Address: " + IP.toString());
  Serial.println("====================\n");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  
  // Update display and client count
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 3000) {
    int newClients = WiFi.softAPgetStationNum();
    if (newClients != connectedClients) {
      connectedClients = newClients;
      Serial.print("Connected clients: ");
      Serial.println(connectedClients);
      updateOLED();
    }
    lastUpdate = millis();
  }
  
  yield(); // Feed watchdog
}

void updateOLED() {
  if (!oledWorking) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Header
  display.setCursor(0, 0);
  display.println("HOME AUTOMATION");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  
  // AP Info
  display.setCursor(0, 14);
  display.print("SSID: ");
  display.println(ap_ssid);
  
  display.setCursor(0, 24);
  display.print("IP: ");
  display.println(WiFi.softAPIP());
  
  // Clients
  display.setCursor(0, 34);
  display.print("Clients: ");
  display.println(connectedClients);
  
  display.drawLine(0, 44, 128, 44, SSD1306_WHITE);
  
  // Relay Status
  display.setCursor(0, 48);
  for (int i = 0; i < 4; i++) {
    display.print("R");
    display.print(i + 1);
    display.print(":");
    display.print(relayStates[i] ? "ON " : "OFF");
    if (i == 1) {
      display.setCursor(0, 56);
    } else if (i < 3) {
      display.print(" ");
    }
  }
  
  display.display();
  Serial.println("OLED updated");
}

void handleRoot() {
  Serial.println("Client connected to portal");
  server.send(200, "text/html", getHTML());
}

void handleLogin() {
  if (server.hasArg("username") && server.hasArg("password")) {
    String user = server.arg("username");
    String pass = server.arg("password");
    
    Serial.print("Login attempt: ");
    Serial.println(user);
    
    if (user == admin_username && pass == admin_password) {
      sessionToken = String(random(100000, 999999));
      String json = "{\"success\":true,\"token\":\"" + sessionToken + "\"}";
      server.send(200, "application/json", json);
      Serial.println("Login successful!");
      return;
    }
  }
  server.send(401, "application/json", "{\"success\":false}");
  Serial.println("Login failed!");
}

bool checkAuth() {
  if (server.hasArg("token") && server.arg("token") == sessionToken && sessionToken.length() > 0) {
    return true;
  }
  return false;
}

void handleToggle() {
  if (!checkAuth()) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  if (server.hasArg("relay")) {
    int relayNum = server.arg("relay").toInt();
    
    if (relayNum >= 1 && relayNum <= 4) {
      relayStates[relayNum - 1] = !relayStates[relayNum - 1];
      
      int pin;
      switch(relayNum) {
        case 1: pin = RELAY1; break;
        case 2: pin = RELAY2; break;
        case 3: pin = RELAY3; break;
        case 4: pin = RELAY4; break;
      }
      
      digitalWrite(pin, relayStates[relayNum - 1] ? HIGH : LOW);
      
      Serial.print("Relay ");
      Serial.print(relayNum);
      Serial.print(" (");
      Serial.print(roomNames[relayNum - 1]);
      Serial.print(") -> ");
      Serial.println(relayStates[relayNum - 1] ? "ON" : "OFF");
      
      saveRelayStates();
      updateOLED();
    }
  }
  
  server.send(200, "text/plain", "OK");
}

void handleStatus() {
  String json = "{";
  json += "\"relay1\":" + String(relayStates[0] ? "true" : "false") + ",";
  json += "\"relay2\":" + String(relayStates[1] ? "true" : "false") + ",";
  json += "\"relay3\":" + String(relayStates[2] ? "true" : "false") + ",";
  json += "\"relay4\":" + String(relayStates[3] ? "true" : "false") + ",";
  json += "\"clients\":" + String(connectedClients) + ",";
  json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleAll() {
  if (!checkAuth()) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  if (server.hasArg("state")) {
    bool state = server.arg("state") == "1";
    
    Serial.print("Setting all relays to: ");
    Serial.println(state ? "ON" : "OFF");
    
    for(int i = 0; i < 4; i++) {
      relayStates[i] = state;
    }
    
    digitalWrite(RELAY1, state ? HIGH : LOW);
    digitalWrite(RELAY2, state ? HIGH : LOW);
    digitalWrite(RELAY3, state ? HIGH : LOW);
    digitalWrite(RELAY4, state ? HIGH : LOW);
    
    saveRelayStates();
    updateOLED();
  }
  
  server.send(200, "text/plain", "OK");
}

void handleScene() {
  if (!checkAuth()) {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  if (server.hasArg("scene")) {
    String scene = server.arg("scene");
    Serial.print("Scene activated: ");
    Serial.println(scene);
    
    if (scene == "movie") {
      relayStates[0] = true;  // Living room
      relayStates[1] = false;
      relayStates[2] = false;
      relayStates[3] = false;
    } else if (scene == "sleep") {
      for (int i = 0; i < 4; i++) relayStates[i] = false;
    } else if (scene == "away") {
      for (int i = 0; i < 4; i++) relayStates[i] = false;
    } else if (scene == "home") {
      relayStates[0] = true;  // Living room
      relayStates[2] = true;  // Kitchen
      relayStates[1] = false;
      relayStates[3] = false;
    }
    
    applyRelayStates();
    saveRelayStates();
    updateOLED();
  }
  
  server.send(200, "text/plain", "OK");
}

void applyRelayStates() {
  digitalWrite(RELAY1, relayStates[0] ? HIGH : LOW);
  digitalWrite(RELAY2, relayStates[1] ? HIGH : LOW);
  digitalWrite(RELAY3, relayStates[2] ? HIGH : LOW);
  digitalWrite(RELAY4, relayStates[3] ? HIGH : LOW);
}

void saveRelayStates() {
  for (int i = 0; i < 4; i++) {
    EEPROM.write(ADDR_RELAY_STATES + i, relayStates[i] ? 1 : 0);
  }
  EEPROM.commit();
  Serial.println("Relay states saved to EEPROM");
}

void loadRelayStates() {
  bool hasData = false;
  for (int i = 0; i < 4; i++) {
    byte val = EEPROM.read(ADDR_RELAY_STATES + i);
    if (val == 1 || val == 0) {
      relayStates[i] = (val == 1);
      hasData = true;
    }
  }
  
  if (hasData) {
    Serial.println("Restored relay states from EEPROM");
    applyRelayStates();
  } else {
    Serial.println("No saved states found, using defaults");
  }
}

String getHTML() {
  return R"=====(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta charset="UTF-8">
<title>Home Automation</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:Arial,sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px}
.container{max-width:900px;margin:0 auto}
.login{background:white;border-radius:20px;padding:40px;max-width:400px;margin:100px auto;box-shadow:0 20px 60px rgba(0,0,0,0.3)}
.login h2{margin-bottom:30px;text-align:center;color:#333}
.login input{width:100%;padding:15px;margin:10px 0;border:2px solid #e5e7eb;border-radius:10px;font-size:1em}
.login button{width:100%;padding:15px;background:linear-gradient(135deg,#667eea,#764ba2);color:white;border:none;border-radius:10px;font-size:1.1em;font-weight:600;cursor:pointer;margin-top:20px}
.header{text-align:center;color:white;margin-bottom:30px}
.header h1{font-size:2.5em;text-shadow:2px 2px 4px rgba(0,0,0,0.3);margin-bottom:10px}
.info{background:rgba(255,255,255,0.2);border-radius:15px;padding:15px;margin-bottom:30px;color:white;text-align:center}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:20px;margin-bottom:30px}
.card{background:white;border-radius:20px;padding:30px;box-shadow:0 10px 30px rgba(0,0,0,0.2);transition:transform 0.3s}
.card:hover{transform:translateY(-5px)}
.room-name{font-size:1.4em;font-weight:600;color:#333;margin-bottom:15px}
.status{display:inline-block;padding:8px 16px;border-radius:20px;font-size:0.9em;font-weight:600;margin-bottom:15px}
.status.on{background:#10b981;color:white}
.status.off{background:#e5e7eb;color:#6b7280}
.btn{width:100%;padding:15px;border:none;border-radius:12px;font-size:1.1em;font-weight:600;cursor:pointer;transition:all 0.3s;margin:5px 0}
.btn-on{background:linear-gradient(135deg,#10b981,#059669);color:white}
.btn-off{background:linear-gradient(135deg,#ef4444,#dc2626);color:white}
.scenes{display:flex;gap:10px;margin-bottom:30px;flex-wrap:wrap}
.scene-btn{flex:1;min-width:120px;padding:15px;background:rgba(255,255,255,0.95);border:none;border-radius:15px;font-weight:600;cursor:pointer}
.controls{text-align:center}
.btn-all{background:linear-gradient(135deg,#667eea,#764ba2);color:white;padding:15px 30px;border:none;border-radius:12px;font-size:1.1em;font-weight:600;cursor:pointer;margin:10px}
#loginPage{display:block}
#mainPage{display:none}
</style>
</head>
<body>
<div id="loginPage" class="login">
<h2>🔐 Login</h2>
<input type="text" id="username" placeholder="Username" value="admin">
<input type="password" id="password" placeholder="Password" value="admin123">
<button onclick="login()">Login</button>
</div>
<div id="mainPage">
<div class="container">
<div class="header">
<h1>🏠 Home Automation</h1>
</div>
<div class="info">
<p>Connected to: <strong>HomeAutomation</strong></p>
<p>Clients: <strong id="clients">0</strong></p>
</div>
<div class="scenes">
<button class="scene-btn" onclick="setScene('movie')">🎬 Movie</button>
<button class="scene-btn" onclick="setScene('sleep')">😴 Sleep</button>
<button class="scene-btn" onclick="setScene('away')">🚪 Away</button>
<button class="scene-btn" onclick="setScene('home')">🏡 Home</button>
</div>
<div class="grid" id="roomsGrid"></div>
<div class="controls">
<button class="btn-all" onclick="toggleAll(true)">All ON</button>
<button class="btn-all" onclick="toggleAll(false)">All OFF</button>
</div>
</div>
</div>
<script>
const rooms=['Living Room','Bedroom','Kitchen','Bathroom'];
const icons=['🛋️','🛏️','🍳','🚿'];
let token='';
function login(){
const u=document.getElementById('username').value;
const p=document.getElementById('password').value;
fetch('/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'username='+encodeURIComponent(u)+'&password='+encodeURIComponent(p)})
.then(r=>r.json())
.then(d=>{
if(d.success){
token=d.token;
document.getElementById('loginPage').style.display='none';
document.getElementById('mainPage').style.display='block';
createRoomCards();
updateUI();
setInterval(updateUI,3000);
}else{
alert('Invalid credentials! Use admin/admin123');
}
}).catch(e=>alert('Connection error'));
}
function createRoomCards(){
const grid=document.getElementById('roomsGrid');
grid.innerHTML='';
for(let i=0;i<4;i++){
const card=document.createElement('div');
card.className='card';
card.innerHTML=`
<div class="room-name">${icons[i]} ${rooms[i]}</div>
<div class="status off" id="status${i+1}">OFF</div>
<button class="btn btn-on" onclick="toggleRelay(${i+1})">Turn ON</button>
`;
grid.appendChild(card);
}
}
function updateUI(){
fetch('/status').then(r=>r.json()).then(d=>{
document.getElementById('clients').textContent=d.clients;
for(let i=0;i<4;i++){
const status=document.getElementById('status'+(i+1));
const btn=document.querySelectorAll('.card')[i].querySelector('.btn');
if(d['relay'+(i+1)]){
status.textContent='ON';
status.className='status on';
btn.textContent='Turn OFF';
btn.className='btn btn-off';
}else{
status.textContent='OFF';
status.className='status off';
btn.textContent='Turn ON';
btn.className='btn btn-on';
}
}
}).catch(e=>console.log('Update error'));
}
function toggleRelay(num){
fetch('/toggle?relay='+num+'&token='+token).then(()=>updateUI());
}
function toggleAll(state){
fetch('/all?state='+(state?'1':'0')+'&token='+token).then(()=>updateUI());
}
function setScene(scene){
fetch('/scene?scene='+scene+'&token='+token,{method:'POST'}).then(()=>updateUI());
}
</script>
</body>
</html>
)=====";
}
