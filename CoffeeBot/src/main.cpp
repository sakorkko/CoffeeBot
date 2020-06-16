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

// OLED screen constants
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1 //Share pin with arduino reset
#define SEALEVELPRESSURE_HPA (1013.25)

// HX711 weight scale constants
#define LOADCELL_DOUT_PIN 4
#define LOADCELL_SCK_PIN 5
#define PAN_GONE_THRESHOLD 500

// Button pins
#define BUTTON_PIN    123 //CHANGE THIS

// Define sensors
Adafruit_BME280 bme;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
HX711 scale;

// Globals
float temperature;
float humidity;
float pressure;
float altitude; 
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

void tempChange(void);

float getWeight(void);

float getTemperature(void);

boolean calculateChanges(void);

float average(float * array, int len);

void updateScreen(void);

void serialLogAllData(void);

void tempChange(void) {
  if (temperature_mean[0] < temperature_mean[1]) { // RATIO CONSTANT
    if (brew) {
      // Brewing done
      brew = false;
    }
    else {
      if (getTemperature() < 35) { // CONSTANT
        // Coffee too cold
        cold = true;
      }
    }
  }
  //Log: data,weight,temp
  //Calculate: usage for current time
  //Broadcast
  updateScreen(); // Note, maybe add updating screen elsewhere also
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
  float temperature_threshold = 20; // CONSTANT
  float weight_threshold = 200; // CONSTANT
  weight_mean[0] = average(weight_history, 10);
  weight_mean[1] = average((weight_history + 10), 10); // Can't use range expression in c++
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
  display.setTextSize(1); // 1:1 Pixel scale
  display.setTextColor(SSD1306_WHITE); // White text
  display.setCursor(0,0); // Top-left

  // Print to serial monitor
  Serial.print('Printing to the screen, the time is: ');
  Serial.print(now());
  Serial.print('\n');

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
}

void setup() {
  Serial.begin(9600);
  mlx.begin();
  bme.begin(0x76);
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  pinMode(BUTTON_PIN, INPUT);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); //Loop forever
  }

  display.display();  // The library initializes this with an Adafruit splash screen.
  delay(1000);
  display.clearDisplay();

  // Zeroing 
  zeroed_weight = getWeight();
  zeroed_temperature = getTemperature();
}

void loop() { 
  delay(1000);
  serialLogAllData(); // For debug purposes, look at logs and stuff
  if (digitalRead(BUTTON_PIN)) {
    // Log trashing
    // Calculate wasted coffee
    brew = false;
    empty = true;
    cold = false;
  }
  else{
    if (calculateChanges()) {
      if ((weight_mean[0] - weight_mean[1]) < 300) { // CONSTANT
        if (!cold) {
          // CoffeeUsed += weigth change
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
      else if ((weight_mean[0] - weight_mean[1]) > 300) {
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
