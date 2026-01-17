#include <WiFi.h>
#include <SPI.h>

#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>




// Temporary Hardcode (to be gotten elseways later)
#define SSID "SSID"
#define PASSWORD "PASSWORD"
#define CLIENT_IP "10.0.0.???"
#define CLIENT_PORT 8001

// Configurable Constants
#define INTERVAL 500 // how long, in milliseconds, between loops
#define CONNECTION_TIMEOUT 100 // how many milliseconds to wait before timing out the connection
#define PASSIVE_LOOPS -1 // number of loops to wait before updating the screen regardless of major changes to state. Set to -1 to disable.
#define MAX_RETRIES -1 // number of connection retries before declaring the client connection lost. Set to -1 to disable; connection is considered lost after one connection failure.
#define FULL_REFRESH_INTERVAL 25  // do a full refresh after this many partial refreshes. This setting generally does not need to be changed.
#define EPD_W 200 // screen width
#define EPD_H 200 // screen height

// Pin wiring; change if pins are in different places
#define EPD_CS   5
#define EPD_DC   21
#define EPD_RST  22
#define EPD_BUSY 19
#define EPD_SCK 18
#define EPD_MISO -1
#define EPD_MOSI 23


// "Graphics"
#define X_STRING "X"
#define CHECK_STRING "V"

// magic code from ChatGPT
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
  GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);


// state management
enum State {
  WIFI_NOT_CONNECTED,
  CLIENT_NOT_CONNECTED,
  DISPLAY_PATCH
};

struct DrawCtx {
  char WiFi_string[32];
  char Client_string[32];
  char MAC_string[18];
  char Line1[32];
  char Line2[32];
  char Line3[32];
  int Line1_textsize;
  int Line2_textsize;
  int Line3_textsize;
  bool bgBlack;
  bool fullRefresh;
};

enum State state = WIFI_NOT_CONNECTED;

// starting variables
struct DrawCtx ctx = {0};
byte refreshCount = 0; // number of refreshes since last full refresh
byte loops = 0; //number of loops since last refresh
byte retries = 0; // number of times we haven't gotten a connection from the client.
char old_client_response[32] = "";
char current_movement_name[32] = "";
char current_patch_name[32] = "";

WiFiClient client;
byte macAddr[6]; // Will be set once when connected


//helper functions
void drawScreen(const struct DrawCtx* ctx) {
  bool fullRefresh = ctx->fullRefresh | (refreshCount++ % FULL_REFRESH_INTERVAL == 0);
  if (fullRefresh) {
    display.setFullWindow();
  } else {
    display.setPartialWindow(0, 0, EPD_W, EPD_H);
  }

  display.firstPage();
    do {
    if (ctx->bgBlack) {
      display.fillScreen(GxEPD_BLACK);
      display.setTextColor(GxEPD_WHITE);
    } else {
      display.fillScreen(GxEPD_WHITE);
      display.setTextColor(GxEPD_BLACK);
    }

    {
    //WiFi string - top-left corner
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.print(ctx->WiFi_string);
    }
    {
    //Client string - top-right corner
    int16_t x, y;
    int16_t x1, y1, w, h;
    display.setTextSize(1);
    display.getTextBounds(ctx->Client_string, x, y, &x1, &y1, (uint16_t*)&w, (uint16_t*)&h);
    x = EPD_W - w;
    y = 0;
    display.setCursor(x, y);
    display.print(ctx->Client_string);
    }
    {
    //Mac address string - bottom-right corner
    int16_t x, y;
    int16_t x1, y1, w, h;
    display.setTextSize(1);
    display.getTextBounds(ctx->MAC_string, x, y, &x1, &y1, (uint16_t*)&w, (uint16_t*)&h);
    x = EPD_W - w;
    y = EPD_H - h;
    display.setCursor(x, y);
    display.print(ctx->MAC_string);
    }
    {
    //Line strings - centered
    int16_t x, y;
    int16_t x1, y1, w, h;

    // Line2 - the very center
    display.setTextSize(ctx->Line2_textsize); 
    display.getTextBounds(ctx->Line2, 0, 0, &x1, &y1, (uint16_t*)&w, (uint16_t*)&h);
    x = (EPD_W - w) / 2;
    y = (EPD_H - h) / 2;
    display.setCursor(x, y);
    display.print(ctx->Line2);
    int16_t line2h = h; // for future height calculations

    // Line1 - above Line2
    display.setTextSize(ctx->Line1_textsize); 
    display.getTextBounds(ctx->Line1, 0, 0, &x1, &y1, (uint16_t*)&w, (uint16_t*)&h);
    x = (EPD_W - w) / 2;
    y = ((EPD_H - h) / 2) - h - (EPD_H/10); // center, raised to be above it. (With 10% padding)
    display.setCursor(x, y);
    display.print(ctx->Line1);

    // Line3 - below Line2
    display.setTextSize(ctx->Line3_textsize); 
    display.getTextBounds(ctx->Line3, 0, 0, &x1, &y1, (uint16_t*)&w, (uint16_t*)&h);
    x = (EPD_W - w) / 2;
    y = ((EPD_H - h) / 2) + line2h + (EPD_H/10); // center, lowered to be below it. (With 10% padding)
    display.setCursor(x, y);
    display.print(ctx->Line3);    
    }

  } while (display.nextPage());
}

void readClientResponseIntoPatchName(bool* screen_dirty) {
  char client_response[32] = "";
  int cursor = 0;
  while(client.available() && cursor < 31) {
    client_response[cursor] = client.read();
    cursor++;
  }
  client.flush();
  
  if (strcmp(old_client_response, client_response) == 0) {
    //nothing has changed, return early
    return;
  } else {
    //something has changed, need to copy strings and mark dirty
    *screen_dirty = true;
    strcpy(old_client_response, client_response);
  }


  //seperate the client response into two strings based on delimiter
  if (strchr(client_response, '|') == NULL) { // if there is no delimiter
    strcpy(current_patch_name, client_response);
    strcpy(current_movement_name, "");
  } else {
    for(cursor = 0; cursor < strlen(client_response); cursor++) {
      if (client_response[cursor] == '|') {
        cursor++; //skip delimiter
        break; //swap to the second loop
      }
      current_movement_name[cursor] = client_response[cursor];
    }
    for(int difference = cursor; //difference between the cursor for client_response and for current_patch_name
    cursor < strlen(client_response); cursor++) {
      current_patch_name[cursor-difference] = client_response[cursor];
    }
  }
}
  
void sendRequestToClient() {
  char temp[64];
  sprintf(temp, "ESP32 %s Connecting to Client", ctx.MAC_string);
  client.println(temp);
  client.println();
}

// Handidly stolen from PieterP on the arduino forum
const char* wl_status_to_string(int status) {
  switch (status) {
    case WL_NO_SHIELD: return "NO_SHIELD";
    case WL_IDLE_STATUS: return "IDLE_STATUS";
    case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECT_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
  }
}

void setup() {
  // display setup
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI);
  display.init();
  display.setRotation(1);

  Serial.begin(115200);

  WiFi.begin(SSID, PASSWORD);
  WiFi.macAddress(macAddr);

  // Setting MAC String, a constant
  sprintf(ctx.MAC_string, "%02X:%02X:%02X:%02X:%02X:%02X", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
}



void loop() {
  if (millis() % INTERVAL != 0)
    return;
  ctx.fullRefresh = false; // reset it
  bool screen_dirty = (PASSIVE_LOOPS != -1) && (loops++ % PASSIVE_LOOPS == 0);
  int WiFi_status = WiFi.status();

  // Setting state
  enum State to_state;
  if (WiFi_status != WL_CONNECTED) {
    to_state = WIFI_NOT_CONNECTED;
  } else if (!client.connect(CLIENT_IP, CLIENT_PORT, CONNECTION_TIMEOUT)) {
    to_state = CLIENT_NOT_CONNECTED;
  } else {
    to_state = DISPLAY_PATCH;
  }

  
  // Handling network management
  switch(to_state) {
    case WIFI_NOT_CONNECTED:
      // Nothing needed; the runtime will automatically try to connect
      break;
    case CLIENT_NOT_CONNECTED:
      if (retries != -1 && retries < MAX_RETRIES) {
      // retry grace: we allow reconnection up to MAX_RETRIES before moving to CLIENT_NOT_CONNECTED
        retries++;
        screen_dirty = true;
        to_state = DISPLAY_PATCH;
      }
      break;
    case DISPLAY_PATCH:
      sendRequestToClient();
      while (!client.available()); // Simply wait for the client response. I hope it's not too long.
      readClientResponseIntoPatchName(&screen_dirty);
      break;
  }

  if (to_state != state) {
    screen_dirty = true;
    ctx.fullRefresh = true;
    state = to_state;
  }
  
  // Setting Text
  switch(state) {
    case WIFI_NOT_CONNECTED:
      sprintf(ctx.WiFi_string, "WiFi %s (ERRORNO %d)", X_STRING, WiFi_status);
      sprintf(ctx.Client_string, "Client %s", X_STRING);

      ctx.bgBlack = true;
      sprintf(ctx.Line1, "Wifi %s", X_STRING);
      ctx.Line1_textsize = 2;
      sprintf(ctx.Line2, "SSID: %s", SSID);
      ctx.Line2_textsize = 2;
      sprintf(ctx.Line3, "%s", wl_status_to_string(WiFi_status));
      ctx.Line3_textsize = 2;
      break;
    case CLIENT_NOT_CONNECTED:
      sprintf(ctx.WiFi_string, "WiFi %s %d dBm", CHECK_STRING, WiFi.RSSI());
      sprintf(ctx.Client_string, "Client %s", X_STRING);

      ctx.bgBlack = true;
      sprintf(ctx.Line1, "Connecting To");
      ctx.Line1_textsize = 2;
      sprintf(ctx.Line2, "%s", CLIENT_IP);
      ctx.Line2_textsize = 2;
      sprintf(ctx.Line3, ":%d", CLIENT_PORT);
      ctx.Line3_textsize = 2;
      break;
    case DISPLAY_PATCH:
      sprintf(ctx.WiFi_string, "WiFi %s", CHECK_STRING);
      sprintf(ctx.Client_string, "Client %s", CHECK_STRING);
      //retry grace
      if (retries > 0) {
        sprintf(ctx.Client_string, "Client %s (%d)", X_STRING, retries);
      }

      ctx.bgBlack = false;
      strcpy(ctx.Line1, current_movement_name);
      ctx.Line1_textsize = 5;
      strcpy(ctx.Line2, "");
      ctx.Line2_textsize = 1;
      strcpy(ctx.Line3, current_patch_name);
      ctx.Line3_textsize = 11;
      break;
  }
  if (screen_dirty) {
    drawScreen(&ctx);
  }
  
}



