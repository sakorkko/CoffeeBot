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

// Oled screen constants
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1 //Share pin with arduino reset
#define SEALEVELPRESSURE_HPA (1013.25)

// HX711 weight scale constants
#define LOADCELL_DOUT_PIN = 4
#define LOADCELL_SCK_PIN = 5

// Button
#define BUTTON_PIN = 123

// Define sensors
Adafruit_BME280 bme;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
HX711 scale;

// globals
float temperature;
float humidity;
float pressure;
float altitude; 
boolean brew;
boolean empty;
boolean cold;
float zeroed_weight;
double zeroed_temperature;

void tempChange(void);

float getWeight(void);

double getTemperature(void);

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

  display.display();  // the library initializes this with an Adafruit splash screen.
  delay(1000);
  display.clearDisplay();

  //Zeroing 
  zeroed_weight = getWeight();
  zeroed_temperature = getTemperature();
}


void loop() { 
  delay(1000);
  if (digitalRead(BUTTON_PIN)) {
    //Log trashing
    //Calculate wasted coffee
    brew = false;
    empty = true;
    cold = false;
  }
  else{
    if (/*Noticeable changes*/) {
      if (/* Weight change --*/) {
        if (!cold) {
          //coffeeUsed += weigth change
          if (/*current weigth < 0*/) {
            //pan gone ignore data back to wait
          }
          else if (/*current weight > 0*/) {
            tempChange();
          }
          else {
            brew = false;
            empty = true;
          }
        }
      }
      else if (/* Weight change ++*/) {
        if (empty) {
          brew = true;
          empty = false;
          cold = true;
        }
        else {
          tempChange();
        }
      }
      else { //weight change ~0
        tempChange();
      }
    }
    else{
      //pass
    }
  }
}

void tempchange(void) {
  if (/*tempchange --*/) {
    if (brew) {
      //brewing done
      brew = false;
    }
    else {
      if (/*Coffee too cold*/) {
        cold = true;
      }
    }
  }
  //Log: data,weight,temp
  //Calculate: usage for current time
  //Broadcast
  //Print to screen
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

double getTemperature(void) {
  return mlx.readObjectTempC();
}