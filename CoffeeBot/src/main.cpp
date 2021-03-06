#include <Arduino.h>
#include "ArduinoTimer.h"
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <HX711.h>
#include <TimeLib.h>
#include <EEPROM.h>
#include <WiFi.h>
#include "time.h"
#include <SPI.h>

// OLED screen constants
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1 //Share pin with arduino reset

// HX711 weight scale constants
#define LOADCELL_DOUT_PIN 4
#define LOADCELL_SCK_PIN 5

// Thresholds
#define TEMPERATURE_TRESHOLD 10
#define WEIGHT_TRESHOLD 10000
#define PAN_GONE_THRESHOLD 30000
#define WEIGHT_RISING_THRESHOLD 10 // TODO: NEEDS TESTING FOR SURE
#define COLD_COFFEE_TEMPERATURE 35
#define WEIGHT_TO_CL_RATIO 1400

// Flash memory
#define EEPROM_SIZE 128 // change me 
#define WASTED_COFFEE_SLOT 0

// Button pins
#define BUTTON_PIN 15

// EEPROM positions
#define POS_INDEX             0    // uint 16
#define POS_WEIGHT_RATIO      2    // float 32
#define POS_WEIGHT_DIFF       6    // float 32
#define POS_COFFEE_WASTED     10   // float 32
#define POS_COFFEE_USED       14   // float 32
#define POS_COFFEE_BREWED     18   // float 32 
#define POS_ESTIMATE_START    22   // 7 x 24 x 2 x 3 Bytes
#define POS_LOG_START         1030 // remaining spots

// Define sensors 
Adafruit_BME280 bme;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
HX711 scale;

enum machineStates{
  IDLE,
  HEATING,
  COFFEE_READY,
  PAN_GONE
};

enum machineStates currentState;
enum machineStates nextState;

// Globals
boolean brew;
boolean empty;
boolean cold;
uint32_t last_half_hour;
float zeroed_weight;
float zeroed_temperature;
// Checking is done by calculating mean of first and second half of list and comparing
float weight_history[20];
float temperature_history[20];
float weight_mean[2]; //first 10 mean, last 10 mean
float temperature_mean[2]; //first 10 mean, last 10 mean
float used_coffee;
float previous_pan_weight;
float saved_pan_weight;
boolean measurement_taken;

//Wifi settings
const char* ssid     = "OTiT";
const char* wlan_pass = "oh8taoh8ta";

//WiFi server
const uint ServerPort = 23;
WiFiServer Server(ServerPort);
WiFiClient RemoteClient;

//Time settings
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 10800;
const int   daylightOffset_sec = 3600;
uint32_t ntpTime = 0;
unsigned long previousMillis = 0;

//Loop timer
ArduinoTimer SendTimer;

void tempChange(void);
float getWeight(void);
float getTemperature(void);
boolean calculateChanges(void);
float average(float * array, int len);
void updateScreen(void);
void serialLogAllData(void);
void updateHistory(void);
uint32_t getEspTime(void);
void updateEspTime(void);
void listNetworks(void);
void setupEeprom(void);
void checkAndUpdateEstimate(void);
void CheckForConnections(void);
void idleState(void);
void heatingState(void);
void readyState(void);
void missingPanState(void);
 
void CheckForConnections()
{
  RemoteClient = Server.available();
  if (RemoteClient.connected())
  {
    // If we are already connected to another computer, 
    // then reject the new connection. Otherwise accept
    // the connection. 
    Serial.println("Connection accepted");
    // RemoteClient = Server.available();
    char amount[8];
    // dtostrf(current_coffee, 6, 2, amount);
    char dest[50];
    strcpy(dest, "sadffdfddaf");
    strcat(dest, amount);
    const String hello = String("hallsdf ");
    // const char body[] = ;    //" and temperature " + String(temperature_mean[0]); 
    RemoteClient.flush();
    RemoteClient.println("SDFASDF" + String(123));
    // RemoteClient.write(dest, sizeof(dest)); 
    if (RemoteClient.connected())
    {
      RemoteClient.stop();
      Serial.println("Connection switched");
      //Server.available().stop();
    }
  }
}

uint32_t getEspTime(){
  const uint32_t time = ntpTime + ((millis()- previousMillis)/1000);
  // Serial.print(F("getEspTime() => "));
  // Serial.println(time);
  return time;
}

void updateEspTime(){
  struct tm timeinfo;
  time_t current;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  time(&current);
  ntpTime = current;
  previousMillis = millis();
  Serial.print(F("updateEspTime(ntptime) => "));
  Serial.println(ntpTime);
  Serial.print(F("updateEspTime(previousmillis) => "));
  Serial.println(previousMillis);
}

void checkAndUpdateEstimate(void) {
  const uint32_t current_time = getEspTime();
  if (current_time > (last_half_hour + 1800)) {
    Serial.print("Half hour passed, lets calculate weekly estimates. ");
    const uint32_t half = ((current_time / 1800) - (3*24*2)) % (7*48);
    const uint8_t saved_count = EEPROM.readUChar(POS_ESTIMATE_START + half);
    const uint8_t new_count = saved_count + 1;
    const uint16_t saved_amount = EEPROM.readUShort(POS_ESTIMATE_START + half + 1);
    const uint16_t used_coffee_in_cl = used_coffee / WEIGHT_TO_CL_RATIO;
    Serial.println("savedAmount:" + String(saved_amount) + " savedCount:" + String(saved_count) + " newAmountSaved:" + String(used_coffee_in_cl) + " newCountSaved:" + String(new_count));
    if(new_count == 0) {
      Serial.println("Hmm, new_count seems to be 0, skip for now...");
      return;
    }
    const uint16_t new_amount = (saved_amount * saved_count + used_coffee_in_cl) / new_count;
    Serial.println("saving amount: " + String(new_amount));
    EEPROM.writeUChar(POS_ESTIMATE_START + half, new_count);
    EEPROM.writeUShort(POS_ESTIMATE_START + half + 1, new_amount);
    last_half_hour = current_time - current_time % 1800;
  }
  else {
    // Serial.print("Seconds until next half hour triggers ");
    // Serial.println(String(last_half_hour + 1800 - current_time));
  }
}

void setupEeprom(void) {
  EEPROM.begin(4096);
  EEPROM.writeUShort(POS_INDEX, POS_LOG_START);
  EEPROM.writeFloat(POS_WEIGHT_DIFF, 123123);
  EEPROM.writeFloat(POS_WEIGHT_RATIO, 123123);
  EEPROM.writeFloat(POS_COFFEE_BREWED, 150.52);
  EEPROM.writeFloat(POS_COFFEE_USED, 100);
  float sadfds = 122;
  EEPROM.writeFloat(POS_COFFEE_WASTED, sadfds);
  for (size_t i = 0; i < POS_LOG_START; i++)
  {
    EEPROM.writeByte(i, 0);
  }
  if (!EEPROM.commit()) {
    for (;;) Serial.print(F("commit failed"));
  }

  Serial.print(EEPROM.readFloat(POS_COFFEE_WASTED));
  Serial.print("ASDASDASDSADSADASDASDSADASDASDDASDSDADSDAS"); 
}

void tempChange(void) {
  if (temperature_mean[1] - temperature_mean[0] > TEMPERATURE_TRESHOLD) {
    Serial.println("Temperature decreased...");
    if (brew) {
      Serial.println("Brewing done.");
      brew = false;
    }
    else {
      if (getTemperature() < COLD_COFFEE_TEMPERATURE) {
        Serial.println("Coffee too cold now.");
        cold = true;
      }
    }
  }
}

float getWeight(void) {
  while(!scale.is_ready()) {
    Serial.println(F("Waiting for weight sensor"));
    delay(500);
  }
  return scale.read();
}

float getTemperature(void) {
  return float(mlx.readObjectTempC());
}

boolean calculateChanges(void) {
  // True if noticeable changes, false if not
  weight_mean[0] = average(weight_history, 10);
  weight_mean[1] = average((weight_history + 10), 10);
  temperature_mean[0] = average(temperature_history, 10);
  temperature_mean[1] = average((temperature_history + 10), 10);
  previous_pan_weight = average((weight_history + 15), 5);
  if (abs(weight_mean[0] - weight_mean[1]) > WEIGHT_TRESHOLD)
    return true;
  if (abs(temperature_mean[0] - temperature_mean[1]) > TEMPERATURE_TRESHOLD)
    return true;
  return false;
}

float average (float * array, int len) {
  double sum = 0;
  for (int i = 0 ; i < len ; i++)
    sum += double(array [i]);
  return ((float(sum)) / (float(len)));
}

void updateScreen(void) {
    // Prepare display
    display.clearDisplay();
    display.setTextSize(1); // todo make display prettier, btw display size is 128x64
    display.setTextColor(SSD1306_WHITE); // White text
    display.setCursor(0,0); // Top-left

    switch(currentState){
        case IDLE:
            display.println(" __\\/__      ZZZzzzZ ");
            display.println("[.z__z.] -   ZZzzZZ  ");
            display.println(" [_--_]              ");
            break;
        case HEATING:
            display.println(" __\\/__   Coffee goes");
            display.println("[.O__O.] -  BRRRRRRRR");
            display.println(" [_--_]              ");
            break;
        case COFFEE_READY:
            display.println(" __\\/__    Coffee is ");
            display.println("[.^__^.] - now ready!");
            display.println(" [_--_]              ");
            break;
        case PAN_GONE:
            display.println(" __\\/__  GIVE BACK MY");
            display.println("[.*__*.] -    PAN!   ");
            display.println(" [_--_]              ");
            break;
    }

    const float used = used_coffee / WEIGHT_TO_CL_RATIO;
    const float tot_used = EEPROM.readFloat(POS_COFFEE_USED);
    const float tot_brew = EEPROM.readFloat(POS_COFFEE_BREWED);

    display.println("Current brew:");
    display.print(F("drank: "));
    display.print(used);
    display.println(" cl");

    display.println("Total:");
    display.print(F("drank: "));
    display.print(tot_used);
    display.println(" cl");
    display.print(F("brewed: "));
    display.print(tot_brew);
    display.println(" cl");

    // Input data to display
//    display.println("CoffeeBot");
//    display.print(F("Time: "));
//    display.println(now());
//    display.print(F("Temp: "));
//    display.println(temperature_history[0]);
//    display.print(F("Weigth: "));
//    display.println(weight_history[0]);

    // Print to display
    display.display();
}

void serialLogAllData(void) {
  Serial.print("Used coffee: ");
  Serial.println(used_coffee);

  // Serial.print(" Weight mean0: ");
  // Serial.print(weight_mean[0]);
  //Serial.print(" Weight mean1: ");
  // Serial.print(weight_mean[1]);
  // Print everything here
  // Serial.print("Weight history ");
  // for (int i = 0 ; i < 20 ; i++) {
  //   Serial.print(weight_history[i]);
  //   Serial.print(' ');
  // };
  // Serial.println();
  // Serial.print("Temperature history ");
  // for (int i = 0 ; i < 20 ; i++) {
  //   Serial.print(temperature_history[i]);
  //   Serial.print(' ');
  // };
  // Serial.println();
  // Serial.print("Zeroed weight ");
  // Serial.print(zeroed_weight);
  // Serial.println();
  // Serial.print("Zeroed temperature ");
  // Serial.print(zeroed_temperature);
  // Serial.println();
  // Serial.println("brew " + brew);
  // Serial.println("epmty " + empty);
  // Serial.println("cold " + cold);


  // Serial.println("Weight_mean " + String(weight_mean[0]) + " " + String(weight_mean[1]));
  // Serial.println("Temperature mean " + String(temperature_mean[0]) + " " + String(temperature_mean[1]));
  // Serial.println("Current time " + String(getEspTime()));


  // Serial.print("EEPROM"); // todo make eeprom read write work
  // for (int i = 0 ; i < EEPROM_SIZE ; i++) {
  //   Serial.print(EEPROM.read(i));
  // };
  // for (int i = 0 ; i < EEPROM_SIZE ; i++) {
  //   Serial.print(EEPROM.readFloat(i));
  // };
}

void updateHistory(void) {
  float temperature = getTemperature();
  float weight = getWeight();
  memcpy(&temperature_history[1], temperature_history, sizeof(temperature_history) - sizeof(float));
  temperature_history[0] = temperature;
  memcpy(&weight_history[1], weight_history, sizeof(weight_history) - sizeof(float));
  weight_history[0] = weight;
}

void printMacAddress() {
  // the MAC address of your Wifi shield
  byte mac[6];                     

  // print your MAC address:
  WiFi.macAddress(mac);
  Serial.print("MAC: ");
  Serial.print(mac[5],HEX);
  Serial.print(":");
  Serial.print(mac[4],HEX);
  Serial.print(":");
  Serial.print(mac[3],HEX);
  Serial.print(":");
  Serial.print(mac[2],HEX);
  Serial.print(":");
  Serial.print(mac[1],HEX);
  Serial.print(":");
  Serial.println(mac[0],HEX);
}

void listNetworks() {
  // scan for nearby networks:
  Serial.println("** Scan Networks **");
  int numSsid = WiFi.scanNetworks();
  if (numSsid == -1)
  { 
    Serial.println("Couldn't get a wifi connection");
    while(true);
  } 

  // print the list of networks seen:
  Serial.print("number of available networks:");
  Serial.println(numSsid);

  // print the network number and name for each network found:
  for (int thisNet = 0; thisNet<numSsid; thisNet++) {
    Serial.print(thisNet);
    Serial.print(") ");
    Serial.print(WiFi.SSID(thisNet));
    Serial.print("\tSignal: ");
    Serial.println(WiFi.RSSI(thisNet));
  }
}

//STATE FUNCTIONS. CHECK SENSORS AND SET NEXT STATE IF NECESSARY
void idleState(){
  if(temperature_mean[0] > COLD_COFFEE_TEMPERATURE){ 
     nextState = HEATING;
   }
}

void heatingState(){
  if(weight_mean[0] < PAN_GONE_THRESHOLD){
    nextState = PAN_GONE;
  }
  else if(abs(weight_mean[0] - weight_mean[1]) < WEIGHT_RISING_THRESHOLD){
    nextState = COFFEE_READY;
  }
  else if(temperature_mean[0] < COLD_COFFEE_TEMPERATURE){
    nextState = IDLE;
  }
}

void readyState() {
  if (!measurement_taken) {
    if (abs(weight_mean[0] - weight_mean[1]) < weight_mean[0] * 0.05) {
      used_coffee = used_coffee + (saved_pan_weight - weight_mean[0]);
      measurement_taken = true;
    }
  }

  else if (temperature_mean[1] < COLD_COFFEE_TEMPERATURE) {
    nextState = IDLE;
  }
  else if (weight_mean[0] < (weight_mean[1] - PAN_GONE_THRESHOLD)) {
    saved_pan_weight = previous_pan_weight;
    nextState = PAN_GONE;
  }
}

void missingPanState(){
  if (weight_mean[0] > (weight_mean[1] + PAN_GONE_THRESHOLD)) {
    nextState = COFFEE_READY;
    measurement_taken = false;
  }
}

void setup() {
  Serial.begin(9600);
  mlx.begin();
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  pinMode(BUTTON_PIN, INPUT);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); //Loop forever
  }

  display.display();  // todo maybe change splash screen
  delay(1000);
  display.clearDisplay();

  setupEeprom();

  // Zeroing 
  zeroed_weight = getWeight();
  zeroed_temperature = getTemperature();
  for (int i = 0 ; i < 20 ; i++) {
    weight_history[i] = zeroed_weight;
    temperature_history[i] = zeroed_temperature;
  };
  used_coffee = 0;
  measurement_taken = true;

  // Wireless
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, wlan_pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("WiFi connected. IP:");
  Serial.println(WiFi.localIP());

  // Time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  updateEspTime();
  getEspTime();
  last_half_hour = getEspTime() - 1799; // todo remove the 1799 part
  Server.begin();
  currentState = IDLE;
  nextState = IDLE;
}

void loop() { 
  if(SendTimer.TimePassed_Milliseconds(300)){
    CheckForConnections();
    updateScreen();
    serialLogAllData();
    updateHistory();
    calculateChanges();
    checkAndUpdateEstimate();

    // State change
    if (nextState != currentState) {
      Serial.println();
      Serial.print("Changing state from ");
      Serial.print(currentState);
      Serial.print(" to ");
      Serial.print(nextState);
      currentState = nextState;
    }
    
    // Reset button
    if (digitalRead(BUTTON_PIN)) {
      currentState = IDLE;
      nextState = IDLE;

      float temp_used = EEPROM.readFloat(POS_COFFEE_USED);
      temp_used = temp_used + (used_coffee / WEIGHT_TO_CL_RATIO);
      EEPROM.writeFloat(POS_COFFEE_USED, temp_used);

      float temp_wasted = EEPROM.readFloat(POS_COFFEE_WASTED);
      temp_wasted = temp_wasted + ((weight_mean[1] - zeroed_weight) / WEIGHT_TO_CL_RATIO);
      EEPROM.writeFloat(POS_COFFEE_WASTED, temp_wasted);

      float temp_brewed = EEPROM.readFloat(POS_COFFEE_BREWED);
      temp_brewed = temp_used + temp_wasted;
      EEPROM.writeFloat(POS_COFFEE_BREWED, temp_brewed);

      EEPROM.commit();
      used_coffee = 0;
    }
 
    // Switch case
    switch(currentState){
      case IDLE:
        idleState();
        break;
      case HEATING:
        heatingState();
        break;
      case COFFEE_READY:
        readyState();
        break;
      case PAN_GONE:
        missingPanState();
        break;
    }
  }
}
