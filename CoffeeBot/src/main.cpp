#include <Arduino.h>
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

// OLED screen constants
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1 //Share pin with arduino reset
#define SEALEVELPRESSURE_HPA (1013.25)

// HX711 weight scale constants
#define LOADCELL_DOUT_PIN 4
#define LOADCELL_SCK_PIN 5

// Thresholds
#define PAN_GONE_THRESHOLD 500
// TODO DEFINE ALL OTHER THRESHOLDS HERE

// Flash memory
#define EEPROM_SIZE 128 // change me 
#define WASTED_COFFEE_SLOT 0

// Button pins
#define BUTTON_PIN    23

// Define sensors 
Adafruit_BME280 bme;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
HX711 scale;

// Globals
boolean brew;
boolean empty;
boolean cold;
float zeroed_weight;
float zeroed_temperature;
// Checking is done by calculating mean of first and second half of list and comparing
float weight_history[20];
float temperature_history[20];
float weight_mean[2]; //first 10 mean, last 10 mean
float temperature_mean[2]; //first 10 mean, last 10 mean
float used_coffee;
float current_coffee;

void tempChange(void);

float getWeight(void);

float getTemperature(void);

boolean calculateChanges(void);

float average(float * array, int len);

void updateScreen(void);

void serialLogAllData(void);

void updateHistory(void);

void tempChange(void) {
  if (temperature_mean[0] < temperature_mean[1]) { // todo RATIO CONSTANT
    if (brew) {
      // Brewing done
      brew = false;
    }
    else {
      if (getTemperature() < 35) { // todo CONSTANT
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
  float temperature_threshold = 20; // todo CONSTANT move me
  float weight_threshold = 200; // todo CONSTANT move me
  weight_mean[0] = average(weight_history, 10);
  weight_mean[1] = average((weight_history + 10), 10); // Can"t use range expression in c++
  temperature_mean[0] = average(temperature_history, 10);
  temperature_mean[1] = average((temperature_history + 10), 10);
  if (abs(weight_mean[0] - weight_mean[1]) > weight_threshold)
    return true;
  if (abs(temperature_mean[0] - temperature_mean[1]) > temperature_threshold)
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
  Serial.print("Printing to the screen, the time is: ");
  Serial.print(now());
  Serial.print("\n");

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
  Serial.print("Weight history");
  for (int i = 0 ; i < 20 ; i++) {
    Serial.print(weight_history[i]);
  };
  Serial.print("Temperature history");
  for (int i = 0 ; i < 20 ; i++) {
    Serial.print(temperature_history[i]);
  };
  Serial.print("Zeroed weight");
  Serial.print(zeroed_weight);
  Serial.print("Zeroed temperature");
  Serial.print(zeroed_temperature);
  Serial.print("brew " + brew);
  Serial.print("epmty " + empty);
  Serial.print("cold " + cold);
  Serial.print("Weight_mean");
  Serial.print(String(weight_mean[0]) + " " + String(weight_mean[1]));
  Serial.print("Temperature_mean");
  Serial.print(String(temperature_mean[0]) + " " + String(temperature_mean[1]));
  Serial.print("EEPROM"); // todo make eeprom read write work
  for (int i = 0 ; i < EEPROM_SIZE ; i++) {
    Serial.print(EEPROM.read(i));
  };
  for (int i = 0 ; i < EEPROM_SIZE ; i++) {
    Serial.print(EEPROM.readFloat(i));
  };
}

void updateHistory(void) {
  memcpy(temperature_history, &temperature_history[1], sizeof(temperature_history) - sizeof(float));
  temperature_history[20] = temperature;
  memcpy(weight_history, &weight_history[1], sizeof(weight_history) - sizeof(float))
  weight_history[20] = weight;
}

void setup() {
  Serial.begin(9600);
  // EEPROM.begin();
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

  // Zeroing 
  zeroed_weight = getWeight();
  zeroed_temperature = getTemperature();
}

void loop() {
  updateScreen();
  delay(1000);
  serialLogAllData(); // todo make printing this prettier
  calculateChanges();
  
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
      if ((weight_mean[0] - weight_mean[1]) < 300) { // todo CONSTANT
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
      else if ((weight_mean[0] - weight_mean[1]) > 300) { // todo constant
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
