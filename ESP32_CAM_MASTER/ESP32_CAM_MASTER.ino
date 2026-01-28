/*
/*
 * Smart Agricultural Disease Monitor - Optimized ESP32-CAM
 * Endpoints: /capture, /capture.jpg, /stream, /sensor
 * Includes: Preferences-based config UI, robust WiFi, reduced FB pressure
 */

// Ensure the ESP32 board package is installed and configured in the Arduino IDE.
// Added comments to guide the user on resolving the missing WiFi.h issue.
// No code changes are made here as the issue is related to the development environment setup.

#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <Preferences.h>

// ============ CONFIGURATION ============
const char* ssid = "Arnabmandal";
const char* password = "hm403496";

// ============ GLOBALS ============
WebServer server(80);
camera_fb_t* capturedFrame = NULL;
Preferences prefs;

// Runtime network settings (may be persisted)
bool useStaticIP = false;
IPAddress staticIP;
IPAddress staticGateway;
IPAddress staticSubnet;
IPAddress staticDNS;

// AI Thinker ESP32-CAM pins
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define FLASH_LED_PIN      4
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Forward declarations
void handleStream();
void handleCapture();
void handleCaptureJPG();
const char* wifiStatusText(wl_status_t s);
void handleReboot();
bool initCamera();

// ============ SETUP ============
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  Serial.println("\n\n=== ESP32-CAM Minimal Streamer ===");

  // Init Camera
  if (!initCamera()) {
    Serial.println("Camera init failed!");
    ESP.restart();
  }
  Serial.println("Camera initialized");

  // Read persisted network settings (if any)
  prefs.begin("cfg", false);
  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  useStaticIP = prefs.getBool("useStatic", false);
  if (useStaticIP) {
    String sip = prefs.getString("ip", "");
    String sg = prefs.getString("gw", "");
    String ss = prefs.getString("sn", "");
    String sd = prefs.getString("dns", "");
    if (sip.length()) staticIP.fromString(sip);
    if (sg.length()) staticGateway.fromString(sg);
    if (ss.length()) staticSubnet.fromString(ss);
    if (sd.length()) staticDNS.fromString(sd);
  }

  if (savedSsid.length() > 0) {
    Serial.println("Using saved WiFi credentials from Preferences");
  }

  // Robust WiFi connect routine: more retries, status messages, and AP fallback
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname("esp32-cam");
  WiFi.disconnect(true);
  delay(100);

  const char* wifi_ssid = (savedSsid.length() > 0) ? savedSsid.c_str() : ssid;
  const char* wifi_pass = (savedPass.length() > 0) ? savedPass.c_str() : password;

  if (useStaticIP) {
    if (staticIP) {
      if (staticDNS) WiFi.config(staticIP, staticGateway, staticSubnet, staticDNS);
      else WiFi.config(staticIP, staticGateway, staticSubnet);
      Serial.print("Using static IP: "); Serial.println(staticIP);
    } else {
      Serial.println("useStaticIP=true but static IP not set — falling back to DHCP");
      useStaticIP = false;
    }
  }

  WiFi.begin(wifi_ssid, wifi_pass);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  Serial.print("Connecting to WiFi");
  int attempts = 0;
  const int MAX_ATTEMPTS = 120;
  while (WiFi.status() != WL_CONNECTED && attempts++ < MAX_ATTEMPTS) {
    Serial.printf(" . (status: %d %s)", WiFi.status(), wifiStatusText(WiFi.status()));
    if (attempts % 20 == 0) WiFi.reconnect();
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi Connected");
    Serial.println("==================================================");
    Serial.print("  >>> Stream URL: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/stream <<<");
    Serial.println("==================================================");
  } else {
    Serial.println("\n✗ WiFi connection failed after timeout");
    Serial.println("Starting AP fallback for configuration: ESP32-CAM-Setup");
    WiFi.softAP("ESP32-CAM-Setup");
    Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  }

  // Server routes
  server.on("/", [](){
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Location", "/stream");
    server.send(302, "text/plain", "Redirecting to stream");
  });
  server.on("/stream", handleStream);
  server.on("/capture", handleCapture);
  server.on("/capture.jpg", handleCaptureJPG);
  server.on("/sensor", [](){
    server.sendHeader("Access-Control-Allow-Origin", "*");
    // Return a minimal but well-formed sensor JSON so the web UI can safely parse values.
    // Replace these placeholders with real sensor reads as needed.
    const char* sensorJson = "{\"soilMoisture\":0.0,\"soilTemp\":0.0,\"airTemp\":0.0,\"airHumidity\":0.0,\"tdsValue\":0.0,\"lightLevel\":0.0,\"timestamp\":\"0000-00-00T00:00:00Z\"}";
    server.send(200, "application/json", sensorJson);
  });

  // AP configuration UI (uses small stack buffers + snprintf)
  server.on("/config", HTTP_GET, [](){
    String s_curSsid = prefs.getString("ssid", ssid);
    String s_curPass = prefs.getString("pass", password);
    bool curUseStatic = prefs.getBool("useStatic", false);
    String s_curIP = prefs.getString("ip", "");
    String s_curGW = prefs.getString("gw", "");
    String s_curSN = prefs.getString("sn", "");
    String s_curDNS = prefs.getString("dns", "");

    char curSsid[64] = {0};
    char curPass[64] = {0};
    char curIP[32] = {0};
    char curGW[32] = {0};
    char curSN[32] = {0};
    char curDNS[32] = {0};

    s_curSsid.toCharArray(curSsid, sizeof(curSsid));
    s_curPass.toCharArray(curPass, sizeof(curPass));
    s_curIP.toCharArray(curIP, sizeof(curIP));
    s_curGW.toCharArray(curGW, sizeof(curGW));
    s_curSN.toCharArray(curSN, sizeof(curSN));
    s_curDNS.toCharArray(curDNS, sizeof(curDNS));

    char page[1024];
    const char* checked = curUseStatic ? "checked" : "";
    int len = snprintf(page, sizeof(page),
      "<html><body><h3>ESP32-CAM Configuration</h3>"
      "<form action='/saveconfig' method='POST'>"
      "SSID: <input name='ssid' value='%s'><br>"
      "Password: <input name='pass' value='%s'><br>"
      "Use Static IP: <input type='checkbox' name='useStatic' %s><br>"
      "Static IP: <input name='ip' value='%s'><br>"
      "Gateway: <input name='gw' value='%s'><br>"
      "Subnet: <input name='sn' value='%s'><br>"
      "DNS: <input name='dns' value='%s'><br>"
      "<input type='submit' value='Save and Connect'></form></body></html>",
      curSsid, curPass, checked, curIP, curGW, curSN, curDNS);
    if (len < 0) len = 0;
    server.send(200, "text/html", page);
  });

  server.on("/saveconfig", HTTP_POST, [](){
    String s_newSsid = server.arg("ssid");
    String s_newPass = server.arg("pass");
    bool newUseStatic = server.hasArg("useStatic");
    String s_newIP = server.arg("ip");
    String s_newGW = server.arg("gw");
    String s_newSN = server.arg("sn");
    String s_newDNS = server.arg("dns");

    char newSsid[64] = {0};
    char newPass[64] = {0};
    char newIP[32] = {0};
    char newGW[32] = {0};
    char newSN[32] = {0};
    char newDNS[32] = {0};

    s_newSsid.toCharArray(newSsid, sizeof(newSsid));
    s_newPass.toCharArray(newPass, sizeof(newPass));
    s_newIP.toCharArray(newIP, sizeof(newIP));
    s_newGW.toCharArray(newGW, sizeof(newGW));
    s_newSN.toCharArray(newSN, sizeof(newSN));
    s_newDNS.toCharArray(newDNS, sizeof(newDNS));

    prefs.putString("ssid", newSsid);
    prefs.putString("pass", newPass);
    prefs.putBool("useStatic", newUseStatic);
    if (newUseStatic && strlen(newIP)) prefs.putString("ip", newIP); else prefs.remove("ip");
    if (newUseStatic && strlen(newGW)) prefs.putString("gw", newGW); else prefs.remove("gw");
    if (newUseStatic && strlen(newSN)) prefs.putString("sn", newSN); else prefs.remove("sn");
    if (newUseStatic && strlen(newDNS)) prefs.putString("dns", newDNS); else prefs.remove("dns");

    char resp[256];
    int r = snprintf(resp, sizeof(resp), "<html><body><h3>Saved configuration. Attempting to connect...</h3><p>SSID: %s</p><p><a href='/config'>Back</a></p></body></html>", newSsid);
    if (r < 0) r = 0;
    server.send(200, "text/html", resp);

    // Try to apply immediately
    delay(200);
    WiFi.disconnect(true);
    delay(100);
    if (newUseStatic && strlen(newIP)) {
      IPAddress sip, sg, ss, sd;
      sip.fromString(newIP);
      if (strlen(newGW)) sg.fromString(newGW);
      if (strlen(newSN)) ss.fromString(newSN);
      if (strlen(newDNS)) sd.fromString(newDNS);
      if (strlen(newDNS)) WiFi.config(sip, sg, ss, sd); else WiFi.config(sip, sg, ss);
    }
    WiFi.begin(newSsid, newPass);
  });

  server.on("/reboot", HTTP_GET, handleReboot);
  server.begin();
  Serial.println("✓ Web server started");
  Serial.println("=== Ready ===\n");
}

// Helper: convert WiFi.status() code to human-readable text
const char* wifiStatusText(wl_status_t s) {
  switch (s) {
    case WL_NO_SHIELD: return "No shield";
    case WL_IDLE_STATUS: return "Idle";
    case WL_NO_SSID_AVAIL: return "No SSID available";
    case WL_SCAN_COMPLETED: return "Scan completed";
    case WL_CONNECTED: return "Connected";
    case WL_CONNECT_FAILED: return "Connect failed";
    case WL_CONNECTION_LOST: return "Connection lost";
    case WL_DISCONNECTED: return "Disconnected";
    default: return "Unknown";
  }
}

// Reboot endpoint
void handleReboot() {
  server.send(200, "text/plain", "Rebooting...");
  delay(200);
  ESP.restart();
}

// ============ MAIN LOOP ============
void loop() {
  server.handleClient();
  delay(10);
}

// ============ CAMERA INIT ============
bool initCamera() {
  Serial.println("Initializing camera...");
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    Serial.println("PSRAM found. Using optimized settings.");
    // Use SVGA for higher resolution when PSRAM is available and allow larger frame buffer
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12; // lower number = higher quality; keep moderate for FPS
    config.fb_count = 2; // double buffer for smoother streaming
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    Serial.println("PSRAM not found. Using fallback settings.");
    // Fallback: still request SVGA but keep conservative memory settings
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 18;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera initialization failed with error 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  // Ensure frame size is applied at sensor level
  s->set_framesize(s, FRAMESIZE_SVGA);
  s->set_vflip(s, 0);
  Serial.println("Camera initialized successfully.");
  return true;
}

// ============ STREAM HANDLER ============
void handleStream() {
  WiFiClient client = server.client();
  if (!client) return;
  client.setNoDelay(true);

  const char HEADER[] = "HTTP/1.1 200 OK\r\n"
                        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                        "Access-Control-Allow-Origin: *\r\n"
                        "Cache-Control: no-cache\r\n"
                        "Connection: keep-alive\r\n\r\n";
  client.write(HEADER, sizeof(HEADER) - 1);

  while (client.connected()) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera frame capture failed. Retrying...");
      delay(100);
      continue;
    }

    // Serial.printf("Streaming frame len=%u\n", (unsigned)fb->len); // Disabled to reduce serial spam

    if (fb->len > 0) {
      char lenBuf[64];
      int headerLen = snprintf(lenBuf, sizeof(lenBuf), "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n", fb->len);
      if (client.write(lenBuf, headerLen) != headerLen || client.write(fb->buf, fb->len) != fb->len || client.write("\r\n", 2) != 2) {
        Serial.println("Client write failed, ending stream.");
        esp_camera_fb_return(fb);
        break;
      }
    }

    esp_camera_fb_return(fb);
    delay(1);
  }

  Serial.println("Client disconnected.");
}

// ============ CAPTURE HANDLERS ============
void handleCapture() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (capturedFrame) { esp_camera_fb_return(capturedFrame); capturedFrame = NULL; }
  capturedFrame = esp_camera_fb_get();
  if (capturedFrame) server.send(200, "text/plain", "OK"); else server.send(500, "text/plain", "Failed to capture frame");
}

void handleCaptureJPG() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!capturedFrame) { server.send(404, "text/plain", "No image captured yet. Call /capture first."); return; }
  WiFiClient client = server.client();
  if (!client) { esp_camera_fb_return(capturedFrame); capturedFrame = NULL; return; }
  client.setNoDelay(true);
  char hdr[128];
  int hdrLen = snprintf(hdr, sizeof(hdr), "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\nAccess-Control-Allow-Origin: *\r\n\r\n", (unsigned)capturedFrame->len);
  if (hdrLen > 0) client.write(hdr, hdrLen);
  client.write(capturedFrame->buf, capturedFrame->len);
  esp_camera_fb_return(capturedFrame);
  capturedFrame = NULL;
}
