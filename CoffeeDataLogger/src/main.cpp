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

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME280 bme;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
float temperature, humidity, pressure, altitude; 

// HX711 circuit wiring
const int LOADCELL_DOUT_PIN = 4;
const int LOADCELL_SCK_PIN = 5;
HX711 scale;

void testdrawstyles(void);

void setup() {
  Serial.begin(9600);
  mlx.begin();
  bme.begin(0x76);
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  // the library initializes this with an Adafruit splash screen.
  display.display();
  delay(2000); // Pause for 2 seconds
  display.clearDisplay();
}

void loop() { 
  testdrawstyles();
  delay(500);
}

void testdrawstyles(void) {
  double tempO = mlx.readObjectTempC();
  double tempA = mlx.readAmbientTempC();
  tempO = mlx.readObjectTempC();
  tempA = mlx.readAmbientTempC();
  temperature = bme.readTemperature();
  humidity = bme.readHumidity();
  double reading = 10;
  if (scale.is_ready()) {
    reading = scale.read();
  } 

  display.clearDisplay();
  display.setTextSize(1);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);       // Draw white text
  display.setCursor(0,0);             // Start at top-left corner

  /* ir temperature */
  display.print(F("Ambient: "));
  display.println(tempA);
  display.print(F("Target:  "));
  display.println(tempO);

  /* bme temperature */
  display.print(F("Humidity:"));
  display.println(humidity);
  display.print(F("Temperat:"));
  display.println(temperature);

  /* hx711 weight */
  display.print(F("Weight:  "));
  display.println(reading);

  /* Time */
  display.print(F("Time:    "));
  display.println(now());

  display.display();


  /* Serial printing */
  /* Time, Ambient, Target, Weight */ 
  Serial.print(now());
  Serial.print(", ");
  Serial.print(tempA);
  Serial.print(", ");
  Serial.print(tempO);
  Serial.print(", ");
  Serial.print(reading);
  Serial.print("\n");



  delay(300);
}