#include <WiFi.h>
#include <WebServer.h>
#include <AccelStepper.h>
#include <Adafruit_NeoPixel.h>
#include "secrets.h"

// --- Configuration ---
const char* ssid     = SECRET_SSID;
const char* password = SECRET_PASS;

const char* www_username = SECRET_USER;
const char* www_password = SECRET_WEB_PASS;

const unsigned long SESSION_TIMEOUT = 300000; 
unsigned long lastActivity = 0;
bool isLocked = true;

#define M1_STEP 4
#define M1_DIR  5
#define M2_STEP 6
#define M2_DIR  7
#define BTN_UP    1
#define BTN_DOWN  2
#define BTN_ESTOP 15
#define RGB_LED_PIN 48 

AccelStepper stepper1(1, M1_STEP, M1_DIR);
AccelStepper stepper2(1, M2_STEP, M2_DIR);
Adafruit_NeoPixel led(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);

bool isStopped = false;
bool webMovingUp = false;
bool webMovingDown = false;
const int runSpeed = 1000; 

void setStatusLED(uint8_t r, uint8_t g, uint8_t b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

bool isAuthenticated() {
  if (!server.authenticate(www_username, www_password)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

void updateActivity() {
  lastActivity = millis();
  isLocked = false;
}

void handleRoot() {
  if (!isAuthenticated()) return;
  updateActivity();

  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no'>";
  html += "<style>body{text-align:center;font-family:sans-serif;background:#121212;color:white;user-select:none;margin:0;padding:10px;} ";
  html += ".status-box{padding:15px;margin:10px auto;width:80%;max-width:350px;border:3px solid #444;font-size:18px;border-radius:10px;text-transform:uppercase;} ";
  html += ".center-stack{display: flex; flex-direction: column; align-items: center; gap: 20px; margin-top: 20px;} ";
  html += "button{width:100px;height:100px;border-radius:50%;border:none;font-weight:bold;font-size:16px;cursor:pointer;touch-action:none;color:white;box-shadow: 0 4px #222; transition: 0.1s;} ";
  html += "button:active{box-shadow: 0 0 #222; transform: translateY(4px);} ";
  html += ".toggle-stop{background:#cc0000;} .toggle-reset{background:#008000;} ";
  html += ".move-btn{background:#333; font-size: 32px; border: 2px solid #555;} ";
  html += ".lock-btn{background:#555; height:80px; width:80px; font-size:12px;} ";
  html += "</style>";
  
  html += "<script>";
  html += "function send(p){ fetch(p).then(r => { if(r.status==401 || p=='/lockout') location.reload(); }); }";
  html += "function startMove(dir){ send('/start?dir=' + dir); }";
  html += "function stopMove(){ send('/stop_move'); }";
  html += "function toggleSys(){ send('/toggle'); }";
  
  html += "setInterval(function() {";
  html += "  fetch('/status').then(r => r.json()).then(data => {";
  html += "    const b = document.getElementById('stat'); b.innerText = data.text; b.style.borderColor = data.color; b.style.color = data.color;";
  html += "    const t = document.getElementById('tog'); ";
  html += "    if(data.stopped){ t.innerText='RESET'; t.className='toggle-reset'; } else { t.innerText='E-STOP'; t.className='toggle-stop'; }";
  html += "    if(data.locked) location.reload();";
  html += "  });";
  html += "}, 800);";
  html += "</script></head><body>";
  
  html += "<h3>Altmill Folding Wall Mount</h3>";
  html += "<div id='stat' class='status-box'>READY</div>";
  
  html += "<div class='center-stack'>";
  html += "  <button id='tog' class='toggle-stop' onclick=\"toggleSys()\">E-STOP</button>";
  html += "  <button class='move-btn' onmousedown=\"startMove('up')\" onmouseup=\"stopMove()\" ontouchstart=\"startMove('up')\" ontouchend=\"stopMove()\">&#9650;</button>";
  html += "  <button class='move-btn' onmousedown=\"startMove('down')\" onmouseup=\"stopMove()\" ontouchstart=\"startMove('down')\" ontouchend=\"stopMove()\">&#9660;</button>";
  html += "  <button class='lock-btn' onclick=\"send('/lockout')\">LOCK</button>";
  html += "</div>";
  
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleStatus() {
  if (millis() - lastActivity > SESSION_TIMEOUT) isLocked = true;

  String json = "{";
  json += "\"stopped\":" + String(isStopped ? "true" : "false") + ", ";
  if (isLocked) json += "\"text\":\"LOCKED\", \"color\":\"#666\", \"locked\":true";
  else if (isStopped) json += "\"text\":\"DISABLED\", \"color\":\"#ff0000\", \"locked\":false";
  else if (stepper1.speed() > 0) json += "\"text\":\"UP\", \"color\":\"#0000ff\", \"locked\":false";
  else if (stepper1.speed() < 0) json += "\"text\":\"DOWN\", \"color\":\"#ffff00\", \"locked\":false";
  else json += "\"text\":\"READY\", \"color\":\"#00ff00\", \"locked\":false";
  json += "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  led.begin();
  led.setBrightness(150);
  
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_ESTOP, INPUT_PULLUP);

  stepper1.setMaxSpeed(2000);
  stepper2.setMaxSpeed(2000);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/start", []() {
    if(!server.authenticate(www_username, www_password)) return;
    updateActivity();
    String dir = server.arg("dir");
    if (dir == "up") webMovingUp = true;
    else if (dir == "down") webMovingDown = true;
    server.send(200);
  });
  server.on("/stop_move", []() { webMovingUp = false; webMovingDown = false; server.send(200); });
  
  // New Toggle Route
  server.on("/toggle", []() {
    if(!server.authenticate(www_username, www_password)) return;
    updateActivity();
    isStopped = !isStopped;
    server.send(200);
  });

  server.on("/lockout", []() {
    isLocked = true; webMovingUp = false; webMovingDown = false;
    server.send(401, "text/plain", "Locked");
  });
  
  server.begin();
  updateActivity();
}

void loop() {
  server.handleClient();
  if (digitalRead(BTN_ESTOP) == LOW) isStopped = true;

  if (isStopped) {
    setStatusLED(255, 0, 0);
    stepper1.setSpeed(0); stepper2.setSpeed(0);
  } else {
    bool moveUp = (digitalRead(BTN_UP) == LOW || (!isLocked && webMovingUp));
    bool moveDown = (digitalRead(BTN_DOWN) == LOW || (!isLocked && webMovingDown));

    if (moveUp) {
      setStatusLED(0, 0, 255);
      stepper1.setSpeed(runSpeed); stepper2.setSpeed(runSpeed);
    } else if (moveDown) {
      setStatusLED(255, 255, 0);
      stepper1.setSpeed(-runSpeed); stepper2.setSpeed(-runSpeed);
    } else {
      setStatusLED(0, 255, 0);
      stepper1.setSpeed(0); stepper2.setSpeed(0);
    }
  }
  stepper1.runSpeed();
  stepper2.runSpeed();
}