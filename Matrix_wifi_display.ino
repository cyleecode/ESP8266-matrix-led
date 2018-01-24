#include <FS.h>
#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library 
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include "LedControlMS.h"         //https://github.com/shaai/Arduino_LED_matrix_sketch
#define NBR_MTX 8 //Number of matrix led module
#define CHAR_WIDTH 6 //CHARACTER PIXEL WIDTH ON DISPLAY
#define CS D8 //MATRIX PIN//
#define DIN D7 //MATRIX PIN//
#define CLK D5 //MATRIX PIN//

LedControl lc = LedControl(DIN, CLK, CS, NBR_MTX); //Setup parameter for matrix led lib

int scrollspd = 0;  //Scrolling speed << MAY write a pull-up function for increase
                    //or decrease the value (increase = slower)/(decrease = faster)

static int AppearOffset = NBR_MTX * 8; //MAX column to display

//MQTT setup variable
char mqtt_server[40];
char mqtt_port[6] = "8080";
char blynk_token[34] = "1234";

bool shouldSaveConfig = false; //flag for saving data

//cloud setup variable
const char* thingSpeakAPI = "api.thingspeak.com";
const char* talkBackAPIKey = "MU9H8MAUZJ4KWLPD";
const char* talkBackID = "8008";
const char* host = "api.thingspeak.com";

void saveConfigCallback () {
  //callback notifying us of the need to save config
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void drawled(byte *pattern , String sentence , int maxlength) {
  //
  //Function of drawing pixel pattern by input string
  //Parameter->( string to contain pattern , character string array for converting , max length of converted character "array x 6 " )
  //
  for (int j = 0; j < (maxlength); j++) {
    //
    //Algorithm of mapping the pixel char by char
    //
    int a = (j  + 1 ) / CHAR_WIDTH;
    int pRow = (j  + 1) % CHAR_WIDTH;
    switch (pRow) {
      case 0:
        pRow = 5;
        a = a - 1;
        break;
  
      default:
        pRow = pRow - 1;
        break;
    }
    int pos = lc.getCharArrayPosition(sentence[a]); //set pixel pattern of current position
    if (pos == 13) {  //13 is the last column to draw pixel
      pattern[j] = alphabetBitmap[10][0];
    }
    else {
      pattern[j] = alphabetBitmap[pos][pRow];
    }
  }
}


void scrolling( byte arrays[] , int spd , int bitlength ) {
  //
  //Algorithm of scrolling display
  //
  for (int k = 0; k < AppearOffset ; k++) {
    for (int i = 0; i < NBR_MTX; i++) {
      for (int j = 0; j < 8; j++) {
        if ((k - j - i * 8) < 0 || (k - j - i * 8) > bitlength - 1) {
          lc.setColumn(NBR_MTX - 1 - i, j, 0x00);
        }
        else  {
          lc.setColumn(NBR_MTX - 1 - i, j, arrays[k - j - i * 8]);
        }
      }
    }
    delay(spd); //speed of scrolling
  }
  for (int k = 0; k < bitlength ; k++) {
    int DispOffset = bitlength - k;

    for (int i = 0; i < NBR_MTX; i++) {
      for (int j = 0; j < 8; j++) {
        if ((j + i * 8) < ( DispOffset)) {
          lc.setColumn(i, 7 - j, arrays[k + j + i * 8]);
        }
        else
          lc.setColumn(i, 7 - j, 0x00);
      }
    }
    delay(spd); //speed of scrolling
  }
}


void lcdWrite( byte arrays[] , int bitlength ) {
  //
  //Algorithm of display non-moving string
  //
  for (int i = 0; i < NBR_MTX; i++) {
    for (int j = 0; j < 8; j++) {
      if ((j + i * 8) < ( bitlength)) {
        lc.setColumn(i, 7 - j, arrays[j + i * 8]);
      }
      else{
        lc.setColumn(i, 7 - j, 0x00);
      }
    }
  }
}

void setup() {
  
  delay(200); //a minor delay for hardware to completely initialize

  //Setting initial matrix led display configuration
  for (int i = 0; i < NBR_MTX; i++) {
    lc.shutdown(i, false);
    /* Set the brightness to a medium values */
    lc.setIntensity(i, 2);
    /* and clear the display */
    lc.clearDisplay(i);
  }

  //Initialize serial comm
  Serial.begin(115200);

  /*Wifi Mangager
   * Setup Wifi SSID and password to connect
   */
  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(blynk_token, json["blynk_token"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
  WiFiManagerParameter custom_blynk_token("blynk", "blynk token", blynk_token, 32);

  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_blynk_token);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration

  /*
   * Display connecting during initiation
   */
  byte connecting[60];
  String display_init = "CONNECTING";
  drawled( connecting , display_init , 60 ); //prepare the display pattern
  scrolling(connecting , 5 , 60);

  /*
   * When set Wifi not available
   */
  display_init = "APMOD";
  drawled( connecting , display_init , 30 );
  lcdWrite( connecting , 30 );
  if (!wifiManager.autoConnect("Admin", "Admin")) {
    //reset and try again, or maybe put it to deep sleep
    delay(2000);
    ESP.reset();
    delay(3000);
  }

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(blynk_token, custom_blynk_token.getValue());
  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["blynk_token"] = blynk_token;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

}


void loop() {
  //main loop content
  WiFiClient client;  // Use WiFiClient class to create TCP connections

  //check for internet connection
  const int httpPort = 80;
  if (!client.connect(host, httpPort)) {
    Serial.println("connection failed");
    return;
  }

  // Construct URI for the request
  String url = "/channels";
  //  url += "?api_key=";
  //  url += "OYKD55NF04JUQ76M";
  url += "/109305/status/last";

  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + thingSpeakAPI + "\r\n" +
               "headers=false" + "\r\n" +
               "Connection: close\r\n\r\n"
              );

  int timeout = millis() + 5;
  while (client.available() == 0) {
    if (timeout - millis() < 0) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return;
    }
  }
  String line;
  // Read all the lines of the reply from server and print them to Serial
  while (client.available()) {
    line = client.readStringUntil('\r');
  }
  
  //convert to non case sensitive
  line.replace('a', 'A');
  line.replace('b', 'B');
  line.replace('c', 'C');
  line.replace('d', 'D');
  line.replace('e', 'E');
  line.replace('f', 'F');
  line.replace('g', 'G');
  line.replace('h', 'H');
  line.replace('i', 'I');
  line.replace('j', 'J');
  line.replace('k', 'K');
  line.replace('l', 'L');
  line.replace('m', 'M');
  line.replace('n', 'N');
  line.replace('o', 'O');
  line.replace('p', 'P');
  line.replace('q', 'Q');
  line.replace('r', 'R');
  line.replace('s', 'S');
  line.replace('t', 'T');
  line.replace('u', 'U');
  line.replace('v', 'V');
  line.replace('w', 'W');
  line.replace('x', 'X');
  line.replace('y', 'Y');
  line.replace('z', 'Z');

  //remove the trigger command comes with input string
  line.remove( line.indexOf("#THINGSPEAK") , 11 );
  line.remove( line.indexOf("THINGSPEAK") , 10 );
  line.remove( line.indexOf("CHGTEXT") , 7 );

  int ptr = line.indexOf("STATUS\":");
  ptr = ptr + 9;
  int dtEnd = line.length() - 1;
  int MAXCOL = dtEnd - ptr;
  char data[MAXCOL];
  for (int i = 0; i <= (MAXCOL); i++) { //from 0 until [data end - data start]
    int j = MAXCOL - i;
    switch (j) {
      case 0:
        data[i] = '\0';
        break;
      default:
        data[i] = line[i + ptr];      //string's wanted data start = data[ i ]
        break;
    }
  }

  //construct the whole string's pattern on matrix led
  int bitLength = MAXCOL * CHAR_WIDTH;
  byte pattern[(bitLength)];

  drawled(pattern, data, bitLength);
  scrolling(pattern , scrollspd , bitLength);

}

