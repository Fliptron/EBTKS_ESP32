#include <FS.h>
#include <SPIFFS.h>
//#include "REMOTEFS.h"
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <SPIFFSEditor.h>
#include <ESPAsyncWiFiManager.h>
#include <HardwareSerial.h>
#include "Update.h"
#include "esp_int_wdt.h"
#include "esp_task_wdt.h"
#include "HTML_pages.h"

//uncomment the following line for EBTKS V3.0 and later pcbs
#define EBTKS3



HardwareSerial HwSerial(1);

const char *http_username = "admin";
const char *http_password = "admin";

char *getLine(void);

const char b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                            "abcdefghijklmnopqrstuvwxyz"
                            "0123456789+/";

// Globals
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dns;

bool restartRequired = false;

char msg_buf[10];
int led_state = 0;
char dest[1000];
int ws_connected = 0;

// Callback: send homepage
void onIndexRequest(AsyncWebServerRequest *request)
{
  IPAddress remote_ip = request->client()->remoteIP();
  Serial.println("[" + remote_ip.toString() +
                 "] HTTP GET request of " + request->url());
  request->send(200,  F("text/html"),PAGE_index);
}

// Callback: send style sheet
void onCSSRequest(AsyncWebServerRequest *request)
{
  IPAddress remote_ip = request->client()->remoteIP();
  Serial.println("[" + remote_ip.toString() +
                 "] HTTP GET request of " + request->url());
  request->send(SPIFFS, "/style.css", "text/css");
}

// Callback: send 404 if requested file does not exist
void onPageNotFound(AsyncWebServerRequest *request)
{
  IPAddress remote_ip = request->client()->remoteIP();
  Serial.println("[" + remote_ip.toString() +
                 "] HTTP GET request of " + request->url());
  request->send(404, "text/plain", "Not found");
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;
    HwSerial.printf("%s\r\n", (const char *)data);
    Serial.print("."); //diag
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
               void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    ws_connected++;
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    if (ws_connected)
    {
      ws_connected--;
    }
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}

/***********************************************************
 * Main
 */

void setup()
{

  Serial.printf("Connected\r\n");

  // Start Serial port

#ifndef EBTKS3
  //EBTKS 2 pcb uses gpio 25 and 26 to communicate with the Teensy4.1
  Serial.begin(115200);
  HwSerial.begin(115200, SERIAL_8N1, 25, 26); //serial connection to the teensy 4.1
#else
  //EBTKS 3.0 pcb uses the default uart pins for uart1
  Serial.begin(115200, SERIAL_8N1, 25, 26); //re-assign the default serial0 port to alternate gpio
  HwSerial.begin(115200, SERIAL_8N1, 3, 1); //assign Serial1 to TXD0,RXD0
#endif

  HwSerial.setRxBufferSize(1024);

  // Make sure we can read the file system
  if (!SPIFFS.begin(true))
  {
    Serial.println("Error mounting SPIFFS");
    while (1)
      ;
  }

  // Make sure we can read the file system
  /*if (!REMOTEFS.begin())
  {
    Serial.println("Error mounting REMOTEFS");
    while (1)
      ;
  }
  File file = REMOTEFS.open("/test", "r");

  if (!file)
  {
    Serial.printf("file didn't open\r\n");
  }
  Serial.printf("File read %c\r\n", file.read());
  */

  //WiFi.softAP(ssid, password);
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  //WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
  AsyncWiFiManager wm(&server, &dns);

  //reset settings - wipe credentials for testing
  //wm.resetSettings();

  // Automatically connect using saved credentials,
  // if connection fails, it starts an access point with the specified name ( "AutoConnectAP"),
  // if empty will auto generate SSID, if password is blank it will be anonymous AP (wm.autoConnect())
  // then goes into a blocking loop awaiting configuration and will return success result

  bool res = wm.autoConnect("EBTKS_AP", "password"); // password protected ap

  if (!res)
  {
    Serial.println("Failed to connect");
    // ESP.restart();
  }
  else
  {
    //if you get here you have connected to the WiFi
    Serial.println("connected...)");

    // Set up mDNS responder:
    // - first argument is the domain name, in this example
    //   the fully-qualified domain name is "esp8266.local"
    // - second argument is the IP address to advertise
    //   we send our IP address on the WiFi network
    if (!MDNS.begin("esp32_ebtks"))
    {
      Serial.println("Error setting up MDNS responder!");
      while (1)
      {
        delay(1000);
      }
    }
    Serial.println("mDNS responder started");

    // On HTTP request for root, provide index.html file
    //server.on("/", HTTP_GET, onIndexRequest);

    //server.serveStatic("/file", SPIFFS, "/").setDefaultFile("index.html");
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(200, F("text/html"), PAGE_index); });

    // On HTTP request for style sheet, provide style.css
    server.on("/style.css", HTTP_GET, onCSSRequest);

    // Handle requests for pages that do not exist
    server.onNotFound(onPageNotFound);

    //init ota page
    server.on("/updater", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(200, F("text/html"), PAGE_updater); });

    server.on(
        "/updater", HTTP_POST, [](AsyncWebServerRequest *request)
        {
          if (Update.hasError())
          {
            request->send(500, F("Failed updating firmware!"), F("Please check your file and retry!"));
            return;
          }
          request->send(200, F("Successfully updated firmware!"), F("Please wait while the module reboots..."));
          //doReboot = true;
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
        {
          if (!index)
          {
            Serial.println(F("OTA Update Start"));
            Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
          }
          if (!Update.hasError())
          {
            Update.write(data, len);
          }
          if (final)
          {
            if (Update.end(true))
            {
              Serial.println(F("Update Success - device will reboot"));
              restartRequired = true; //loop code will take care of this
            }
            else
            {
              Serial.println(F("Update Failed"));
            }
          }
        });

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    server.addHandler(new SPIFFSEditor(SPIFFS, http_username, http_password));

    // Start web server
    server.begin();

    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);
  }
}

void loop()
{
  char *line;
  // Look for and handle WebSocket data
  ws.cleanupClients();
  if (restartRequired)
  {
    yield();
    delay(1000);    //wait until the web page has been updated
    yield();

    // ESP32 will commit sucide
    esp_task_wdt_init(1, true);
    esp_task_wdt_add(NULL);
    while (true);
  }


  if ((line = getLine()) != NULL)
  {
    //we've got a line of data
    // todo - add crc to the data stream to ensure if is kosher

    if (ws_connected == true)
    {
      ws.textAll(line);
    }
  }
}

#define MAX_RX_BUFF (1024u)

//
//  returns a pointer to the received line of chars
//  else returns NULL if a line hasn't arrived yet
//
char *getLine(void)
{
  static char lineBuff[MAX_RX_BUFF];
  static int currNdx = 0;

  while (HwSerial.available()) //grab as many chars a we can
  {
    char ch = HwSerial.read();
    if (ch == 10) //if line feed
    {
      lineBuff[currNdx] = 0; //terminate the string
      currNdx = 0;
      return lineBuff;
    }
    lineBuff[currNdx++] = ch;

    if (currNdx >= MAX_RX_BUFF) //overflow? reset the line buffer and discard previous chars
    {
      lineBuff[0] = 0;
      currNdx = 0;
    }
  }
  return NULL;
}

inline void a3_to_a4(unsigned char *a4, unsigned char *a3)
{
  a4[0] = (a3[0] & 0xFC) >> 2;
  a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xF0) >> 4);
  a4[2] = ((a3[1] & 0x0F) << 2) + ((a3[2] & 0xC0) >> 6);
  a4[3] = (a3[2] & 0x3F);
}

int base64_encode(char *output, char *input, int inputLen)
{
  int i = 0, j = 0;
  int encLen = 0;
  unsigned char a3[3];
  unsigned char a4[4];

  while (inputLen--)
  {
    a3[i++] = *(input++);
    if (i == 3)
    {
      a3_to_a4(a4, a3);

      for (i = 0; i < 4; i++)
      {
        output[encLen++] = b64_alphabet[a4[i]];
      }

      i = 0;
    }
  }

  if (i)
  {
    for (j = i; j < 3; j++)
    {
      a3[j] = '\0';
    }

    a3_to_a4(a4, a3);

    for (j = 0; j < i + 1; j++)
    {
      output[encLen++] = b64_alphabet[a4[j]];
    }

    while ((i++ < 3))
    {
      output[encLen++] = '=';
    }
  }
  output[encLen] = '\0';
  return encLen;
}

char base64Buff[16384];
char screen[512];

void dumpCrtAlphaAsJSON(char *dest)
{

  memset(screen, 't', 512);

  screen[0] = 'E';
  screen[32] = 'Z';
  screen[511] = 'P';

  base64_encode(base64Buff, screen, 512); //32x16 alpha only
  sprintf(dest, "{\"type\":\"hp85text\",\"image\":\"%s\"}\n", base64Buff);
}

extern "C"
{
  void extSend(const char *fmt, ...)
  {
    va_list arg;
    va_start(arg, fmt);
    Serial.printf(fmt, arg);
    va_end(arg);
  }
}
