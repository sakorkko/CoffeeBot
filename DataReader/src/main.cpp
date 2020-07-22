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

// Weight thresholds
#define PAN_GONE_THRESHOLD 500

// Flash memory
#define EEPROM_SIZE 128 // change me 
#define WASTED_COFFEE_SLOT 0

// Button pins
#define BUTTON_PIN    23 //CHANGE THIS

// Define sensors 
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
HX711 scale;

// Globals
float temperature;
float humidity;
float pressure;
float altitude; 
float weight;
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

float getWeight(void);

float getTemperature(void);

void updateScreen(void);

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

void updateScreen(void) {
  // Prepare display
  display.clearDisplay();
  display.setTextSize(1); // 1:1 Pixel scale
  display.setTextColor(SSD1306_WHITE); // White text
  display.setCursor(0,0); // Top-left

  // Print to serial monitor
  Serial.print(now());
  Serial.print('\n');

  // Input data to display
  if (digitalRead(BUTTON_PIN) == HIGH) {
    display.println("CoffeeBot");
  } 
  display.print(F("Time: "));
  display.println(now());
  display.print(F("Temp: "));
  display.println(mlx.readObjectTempC());
  display.print(F("Weigth: "));
  display.println(weight);

  // Print to display
  display.display();
}

void setup() {
  Serial.begin(9600);
  EEPROM.begin(EEPROM_SIZE);
  mlx.begin();
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  pinMode(BUTTON_PIN, INPUT);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); //Loop forever
  }
  display.display();  // The library initializes this with an Adafruit splash screen.
  delay(1000);
  display.clearDisplay();
}

void loop() { 
  delay(1000);
  temperature = getTemperature();
  weight = getWeight();
  updateScreen();
  
  // digitalWrite(BUTTON_PIN, HIGH);
  // if (digitalRead(BUTTON_PIN) == LOW) {
  //   Serial.print("1");
  // }
  // else {
  //   Serial.print("0");
}
