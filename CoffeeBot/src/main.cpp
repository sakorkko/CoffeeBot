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
#define TEMPERATURE_TRESHOLD 20
#define WEIGHT_TRESHHOLD 200
#define PAN_GONE_THRESHOLD 500
#define TEMP_CHANGE_RATIO 0.01
#define COLD_COFFEE_TEMPERATURE 35
#define WEIGHT_TO_CL_RATIO 500

// Flash memory
#define EEPROM_SIZE 128 // change me 
#define WASTED_COFFEE_SLOT 0

// Button pins
#define BUTTON_PIN 23

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
float current_coffee;

//Wifi settings
const char* ssid     = "panoulu";

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
 
void CheckForConnections()
{
  RemoteClient = Server.available();
  if (Server.hasClient())
  {
    // If we are already connected to another computer, 
    // then reject the new connection. Otherwise accept
    // the connection. 
    Serial.println("Connection accepted");
    RemoteClient = Server.available();
    RemoteClient.write("Hello world"); 
    if (RemoteClient.connected())
    {
      RemoteClient.stop();
      Serial.println("Connection switched");
      //Server.available().stop();
    }
      
    
  }
}

uint32_t getEspTime(){
  return ntpTime + ((millis()- previousMillis)/1000);
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
}

void checkAndUpdateEstimate(void) {
  if (getEspTime() > last_half_hour + 1800) {
    Serial.println(F("Half hour passed, calculating estimates"));
    const uint32_t half = ((getEspTime() / 1800) - (3*24*2)) % (7*48);
    const uint8_t saved_count = EEPROM.readUChar(POS_ESTIMATE_START + half);
    const uint8_t new_count = saved_count + 1;
    const uint16_t saved_amount = EEPROM.readUShort(POS_ESTIMATE_START + half + 1);
    const uint16_t used_coffee_in_cl = used_coffee / WEIGHT_TO_CL_RATIO;

    const uint16_t new_amount = (saved_amount * saved_count + used_coffee_in_cl) / new_count;
    EEPROM.writeUChar(POS_ESTIMATE_START + half, saved_count + 1);
    EEPROM.writeUShort(POS_ESTIMATE_START + half + 1, new_amount);
    updateEspTime();
    const uint32_t current_time = getEspTime();
    last_half_hour = current_time - current_time % 1800;
  }
}

void setupEeprom(void) {
  EEPROM.begin(4096);
  EEPROM.writeUShort(POS_INDEX, POS_LOG_START);
  EEPROM.writeFloat(POS_WEIGHT_DIFF, 123123);
  EEPROM.writeFloat(POS_WEIGHT_RATIO, 123123);
  EEPROM.writeFloat(POS_COFFEE_BREWED, 0);
  EEPROM.writeFloat(POS_COFFEE_USED, 0);
  EEPROM.writeFloat(POS_COFFEE_WASTED, 0);

  if (!EEPROM.commit()) {
    for (;;) Serial.print('commit failed');
  }
}

void tempChange(void) {
  if (temperature_mean[0] < temperature_mean[1] * TEMP_CHANGE_RATIO) {
    if (brew) {
      // Brewing done
      brew = false;
    }
    else {
      if (getTemperature() < COLD_COFFEE_TEMPERATURE) {
        // Coffee too cold
        cold = true;
      }
    }
  }
  //TODO do this loggin below
  //Log: data,weight,temp
  //Calculate: usage for current time
  //Broadcast
  updateScreen(); // Todo, maybe add updating screen elsewhere also
}

float getWeight(void) {
  if (scale.is_ready()) {
    return scale.read();
  }
  else {
    Serial.println(F("Waiting for weight sensor"));
    delay(500);
    return getWeight();
  }
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
  if (abs(weight_mean[0] - weight_mean[1]) > WEIGHT_TRESHHOLD)
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

  // Print to serial monitor
  // Serial.print("Printing to the screen, the time is: ");
  // Serial.print(now());
  // Serial.print("\n");

  // Input data to display
  display.println("CoffeeBot");
  display.print(F("Time: "));
  display.println(now());
  display.print(F("Temp: "));
  display.println(temperature_history[0]);
  display.print(F("Weigth: "));
  display.println(weight_history[0]);

  // Print to display
  display.display();
}

void serialLogAllData(void) {
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
  Serial.print("Weight_mean ");
  Serial.print(String(weight_mean[0]) + " " + String(weight_mean[1]));
  Serial.print("\t\tTemperature_mean ");
  Serial.print(String(temperature_mean[0]) + " " + String(temperature_mean[1]));
  Serial.print("\t\tCurrent time ");
  Serial.print(getEspTime());
  Serial.println();
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

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected.");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  updateEspTime();
  getEspTime();
  Server.begin();

}

void loop() {
  CheckForConnections();

  if(SendTimer.TimePassed_Milliseconds(100)){

    updateScreen();
    updateHistory();
    calculateChanges();
    serialLogAllData(); // todo make printing this prettier
    checkAndUpdateEstimate();
    
    if (digitalRead(BUTTON_PIN)) {
      // Log trashing
      current_coffee += EEPROM.readFloat(WASTED_COFFEE_SLOT);
      EEPROM.writeFloat(WASTED_COFFEE_SLOT, current_coffee);
      EEPROM.commit();
      current_coffee = 0;
      used_coffee = 0;
      brew = false;
      empty = true;
      cold = false;
    }
    else{
      if (calculateChanges()) {
        if ((weight_mean[0] - weight_mean[1]) < WEIGHT_TRESHHOLD) {
          if (!cold) {
            // CoffeeUsed += weigth change // check that this is in state machine todo
            if (getWeight() + PAN_GONE_THRESHOLD < zeroed_weight) {
              // Pan gone ignore data back to wait
            }
            else if (getWeight() - PAN_GONE_THRESHOLD > zeroed_weight) {
              tempChange();
            }
            else {
              brew = false;
              empty = true;
            }
          }
        }
        else if ((weight_mean[0] - weight_mean[1]) > WEIGHT_TRESHHOLD) {
          if (empty) {
            brew = true;
            empty = false;
            cold = true;
          }
          else {
            tempChange();
          }
        }
        else { // Weight change ~0
          tempChange();
        }
      }
      else{
        // Pass
      }
    }
  }
}
