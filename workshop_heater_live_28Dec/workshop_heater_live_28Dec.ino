/*
Workshop heating controller - version with OTA
Serves an HTML page which shows temperature and allows the heater to be turned on and off.
There is a minimum temperature below which the heater is turned on automatically.
Uses Ticker to generate 1 second count to increment watchdog.
Also a 60 second counter for updating the temperature.
Latest addition is the ability to update over the air.
Removed all Serial output.
 */

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <OneWire.h>  // required for the communication with the DS18B20
#include <DallasTemperature.h> //handles the data from the DS18B20
#include <Ticker.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>


const char *ssid = "workshop";
const char *password = "workshop";

ESP8266WebServer server(80); //create a webserver object 'server'

Ticker secondTick; //create a Ticker object 'secondTick'
Ticker minuteTick; //create a Ticker object 'minuteTick'


#define ONE_WIRE_BUS D7 // pin 7 for DS18B20

// Setup a oneWire instance
OneWire oneWire(ONE_WIRE_BUS);

// Pass oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);//DS18B20 object

#define relay D5 //relay on pin 5
char page[600];//the html page to send
int temperature = 100;// holds the current temperature
boolean heater_status = FALSE;//tells us whether the heater is on or off
boolean manual_control = FALSE;// used to track if the heater was turned on manually
int watchdogCount = 0;// watchdog is incremented every second by Ticker.  Bites at 5 seconds.
int autoHeaterOnCount = 0;//counts the times the low temperture threshold triggers heater on

//Called by Ticker every second and increments the WDT
void ISRwatchdog(){
  watchdogCount ++;
  if (watchdogCount == 5) {
    ESP.reset();
  }
}

// Updates the temperature when called by Ticker each minute
void ISRgetTemperature(){
  sensors.requestTemperatures(); // Send the command to get temperatures
  delay(20);
  temperature = sensors.getTempCByIndex(0);
  if (temperature < -20 || temperature > 84){ //check for in range temperature value
    temperature = 20;
  }
}

//=======================================================================================
//server handling routines

void handleRoot() {
  buildPage(heater_status);
  server.send (200, "text/html",page);
}//end handleRoot

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }

  server.send ( 404, "text/plain", message );
}//end handleNotFound

void buildPage(boolean on_off) {
    const char* text = "";
    if (on_off) {
      text = "ON ";
    }
    else {
      text = "OFF";
    }
    
    snprintf (page, 600,

"<html>\
  <head>\
    <title>Workshop</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <h1>Workshop Temperature Controller</h1>\
    <p>Auto On Count: %d</p>\    
    <p>Temperature: %02d</p>\
    <p>Heater status: ",
    autoHeaterOnCount, temperature
  );
  strcat(page, text);
  strcat(page, "<p>\
  <a href=\"HEATER_ON\"><button style=\"font-size:30;width:150;height:100;\
  background-color:red\">ON</button></a>\
  <a href=\"HEATER_OFF\"><button style=\"font-size:30;width:150;height:100;\
  background-color:green\">OFF</button></a>\
  </p></body></html>");
}//end buildPage
//========================================================================================

void setup () {
  pinMode (relay, OUTPUT);
  digitalWrite (relay, LOW); //make sure relay is off
  sensors.begin();
  WiFi.mode(WIFI_STA);
  WiFi.begin (ssid, password);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED ) {
    delay (500);
  }

  MDNS.begin ( "workshop_heater");
//=================================================================================
  //setup server
  server.on ("/", handleRoot );

  //Deal with turning heater on
  server.on("/HEATER_ON", [](){
  digitalWrite(relay, HIGH);
  heater_status = TRUE;
  manual_control = TRUE;
  buildPage(heater_status);
  server.send(200, "text/html", page);
  });

//deal with turning heater off
  server.on("/HEATER_OFF", [](){
  digitalWrite(relay, LOW);
  heater_status = FALSE;
  manual_control = FALSE;
  buildPage(heater_status);
  server.send(200, "text/html", page);
  });

//deal with page not found
  server.onNotFound (handleNotFound);
  
  server.begin();
  MDNS.addService("http", "tcp", 80);


//start the watchDog timer
  secondTick.attach(1, ISRwatchdog);

  ISRgetTemperature(); //get the temperature 
//start the getTemperature timer to update every minute
minuteTick.attach(60, ISRgetTemperature);

//=======================================================================================
//Set up OTA


// Hostname defaults to esp8266-[ChipID]
// ArduinoOTA.setHostname("workshop_heater");

// No authentication by default
//ArduinoOTA.setPassword((const char *)"123");

ArduinoOTA.onStart([]() {
  noInterrupts();
  digitalWrite(relay, LOW);
});
ArduinoOTA.onEnd([]() {
  interrupts();
  digitalWrite(relay, LOW);
});

ArduinoOTA.onError([](ota_error_t error) {
  ESP.reset();
});
ArduinoOTA.begin();

//=================================================================================
}//end of setup

void loop() {
  server.handleClient();
  MDNS.update();
  ArduinoOTA.handle();
  watchdogCount = 0;
  if (temperature < 8  && heater_status == FALSE && manual_control == FALSE) {
    heater_status = TRUE;
    autoHeaterOnCount += 1;
    digitalWrite(relay, HIGH);
  }
  if (temperature >= 10 && heater_status == TRUE && manual_control == FALSE) {
    heater_status = FALSE;
    digitalWrite(relay, LOW);
  }
  //turn heater off if temperature reaches the upper set point
  if (temperature > 22){
    heater_status = FALSE;
    digitalWrite(relay, LOW);
  }

}//end of main loop



