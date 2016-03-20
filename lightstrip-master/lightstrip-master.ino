#include <Wire.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>

#include "SmartThings.h"

#define DEBUG_ENABLED      1

// Pins used:

// Digital

// 0  - Wixel RX
// 1  - Wixel TX

#define PIN_THING_RX       3
#define PIN_THING_TX       2

// 10 - SPI SS
// 11 - SPI MOSI
// 12 - SPI MISO
// 13 - SPI SCK / Wixel LED

#define PIN_WORKING_LED    13

// Analog

// 1 - Humidity
#define HUMIDITY_IN 1
// 2 - Ambient
#define AMBIENT_IN 2
// 4 - SDA (Temperature)
// 5 - SDL (Temperature)

// SPI Addressing

#define TEMPERATURE_ADDRESS (0x91 >> 1)

// EEPROM

#define EEPROM_ADDRESS_H 2
#define EEPROM_ADDRESS_S 3
#define EEPROM_ADDRESS_V 4

////////////////////////////////////////////////////////////////////////////
// Utility and memory reporting.

uint8_t parseHex(uint8_t ch) 
{
  if (ch >= (uint8_t)'0' && ch <= (uint8_t)'9')
    return ch - (uint8_t)'0';  
  if (ch >= (uint8_t)'A' && ch <= (uint8_t)'F')
    return ch - (uint8_t)'A' + 10;  
  if (ch >= (uint8_t)'a' && ch <= (uint8_t)'f')
    return ch - (uint8_t)'a' + 10;
  return 0;
}
  
void printHex(Print& p, uint8_t v)
{
  if (v < 16)
    p.print("0");
  p.print(v, HEX);
}

////////////////////////////////////////////////////////////////////////////
// Sensor reading.

class SensorService
{
public:
  SensorService()
    : _lastCheck(0)
  {
  }

  void setup()
  {
    Wire.begin();
    
    ambient = temperature = humidity = 0;
  }

  void update(unsigned long mills)
  {
    if (_lastCheck == 0 || mills - _lastCheck > 5000)
    {
      _lastCheck = mills;

      int amb = analogRead(AMBIENT_IN);
      int hum = analogRead(HUMIDITY_IN);

      Wire.requestFrom(TEMPERATURE_ADDRESS, 2);
      if (Wire.available() >= 2)  // if two bytes were received
      {
        byte msb = Wire.read();
        byte lsb = Wire.read();

        int t = ((msb << 8) | lsb) >> 4;

        float realTemperature = t * 0.0625;
        
        float vout = (hum / 1023.0 * 5.0);
        
        float sensorRH = (vout / 4.8 - 0.16) / 0.0062; 
        float trueRH = sensorRH / (1.0546 - 0.00216 * realTemperature);
        
        ambient = amb;
        temperature = realTemperature * 9 / 5 + 32;
        humidity = trueRH;
      }
    }
  }

  void printSensors(Print& p)
  {
    p.println(ambient);
    p.println(temperature);
    p.println(humidity);
  }

private:
  unsigned long _lastCheck;

public:  
  float ambient;
  float temperature;
  float humidity; 
};

////////////////////////////////////////////////////////////////////////////
// Lighting control.    

class LightingService
{
public:
  void setup()
  {
    _colorH = EEPROM.read(EEPROM_ADDRESS_H);
    _colorS = EEPROM.read(EEPROM_ADDRESS_S);
    _colorV = EEPROM.read(EEPROM_ADDRESS_V);
      
    _lightingChanged = false;
    
    _lastH = 255;
    _lastS = 255;
    _lastV = 255;
    
    _lastLightingUpdate = 0;
    _lastLightingWrite = 0;
    
    Serial.print("Startup colors ");
    
    printHex(Serial, _colorH);
    printHex(Serial, _colorS);
    printHex(Serial, _colorV);
    
    Serial.println();
  }
  
  void writeLightingState()
  {
    EEPROM.write(EEPROM_ADDRESS_H, _colorH);
    EEPROM.write(EEPROM_ADDRESS_S, _colorS);
    EEPROM.write(EEPROM_ADDRESS_V, _colorV);
    
    Serial.println("Colors saved");
  }
  
  void configureColor(int h, int s, int v)
  {
    _colorH = h;
    _colorS = s;
    _colorV = v;
    
    if (_colorH > 239)
      _colorH = 239;      
      
    _lightingChanged = true;
    
    _lastLightingWrite = 0;
  }
  
  void update(unsigned long mills)
  {
    // Check if new color settings need to be saved to EEPROM.
    if (_lightingChanged) 
    {
      if (_lastLightingWrite == 0)
      {
        _lastLightingWrite = mills;
      }
      if (mills - _lastLightingWrite > 30 * 1000) 
      {
        writeLightingState();
        _lightingChanged = false;
      }  
    }
    
    // Check if colors need to be sent to Wixel.
    unsigned long deltaTime = mills - _lastLightingUpdate;
    if (_lastLightingUpdate == 0 || deltaTime > 100) {
      _lastLightingUpdate = mills;
      
      uint8_t effH = _colorH;
      uint8_t effS = _colorS;
      uint8_t effV = _colorV;
      
      if (effH != _lastH || effS != _lastS || effV != _lastV)
      {
        _lastH = effH;
        _lastS = effS;
        _lastV = effV;        
        
        syncToWixel();
      }
    }
  }  
  
private:
  void syncToWixel()
  {
    Serial.print("Wx|S");
    printHex(Serial, _lastH);    
    printHex(Serial, _lastS);    
    printHex(Serial, _lastV);
    Serial.println();
  }
  
private:
  uint8_t _colorH;
  uint8_t _colorS;
  uint8_t _colorV;
  
  uint8_t _lastH;
  uint8_t _lastS;
  uint8_t _lastV;
  
  unsigned long _lastLightingUpdate;
  
  bool _lightingChanged;
  unsigned long _lastLightingWrite;  
};

SensorService sensors;
LightingService lighting;

SmartThingsCallout_t messageCallout;
SmartThings smartthing(PIN_THING_RX, PIN_THING_TX, messageCallout);

// Generate RGB uint32  color.
#define RGB(r, g, b) ((b << 16) | (g << 8) | (r))

// Generate RGB color from HSV.
// H: Hue in the range of [0, 239] 
// S: Saturation in the range of [0, 255]
//      0 produces least saturated color, so modulating V changes result from black to white.
//      255 is the most saturated color, V allows to change result from black to fully saturated value.
// V: Value in the range [0, 255]
unsigned long hsvToRgb(int h, int s, int v) 
{
  unsigned long i, f;

  unsigned long m, n, v32 = v;
  
  if ((s < 0) || (s > 255) || (v < 0) || (v > 255))
  {
    return RGB(0, 0, 0);
  }
  if (h < 0 || h > 239) 
  {
    return RGB(255, 255, 255);
  }
  i = h / 40;
  f = h - i * 40;
  if (!(i & 1))
  {
    f = 40 - f;
  }

  m = (v * (255 - s)) / 256;
  n = (v * (255 - s * f / 40)) / 256;

  Serial.println(i);
  Serial.println(v32);
  Serial.println(m);
  Serial.println(n);
  
  switch (i) 
  {
    case 6:
    case 0: return RGB(v32, n, m);
    case 1: return RGB(n, v32, m);
    case 2: return RGB(m, v32, n);
    case 3: return RGB(m, n, v32);
    case 4: return RGB(n, m, v32);
    case 5: return RGB(v32, m, n);
  }

  // Should never happen.
  return RGB(0, 0, 0);
}

void configureColor(String& message) {
  int h;
  int s;
  int v;
  
  sscanf(message.c_str() + 5, "%d %d %d", &h, &s, &v);

#if DEBUG_ENABLED       
  Serial.print("Configure color ");
  Serial.print(h);
  Serial.print(" ");
  Serial.print(s);
  Serial.print(" ");
  Serial.print(v);
  Serial.println();
#endif        

  unsigned long rgb = hsvToRgb(h, s, v);

Serial.println(rgb);
  Serial.print("RGB ");
  Serial.print((rgb & 0xff) >> 5);
  Serial.print(" ");
  Serial.print(((rgb >> 8) & 0xff) >> 5);
  Serial.print(" ");
  Serial.print(((rgb >> 16) & 0xff) >> 5);
  Serial.println();
  
  smartthing.shieldSetLED((rgb & 0xff) >> 5, ((rgb >> 8) & 0xff) >> 5, ((rgb >> 16) & 0xff) >> 5);

  lighting.configureColor(h, s, v);
}

void messageCallout(String message)
{
#if DEBUG_ENABLED
    Serial.print("Received message: '");
    Serial.print(message);
    Serial.println("' ");
#endif

  if (message.length() > 3) {
    if (message[0] == 'c' && message[1] == 'o' && message[2] == 'l' && message[3] == 'r') {
      configureColor(message);
    }
  }
}

void setup()
{
  Serial.begin(9600);

  pinMode(PIN_WORKING_LED, OUTPUT);

  smartthing.run();
  smartthing.shieldSetLED(0, 0, 2);
  
  //sensors.setup();
  lighting.setup();
}

void loop()
{
  unsigned long mills = millis();

  //sensors.update(mills);
  lighting.update(mills);

  // Run smartthing logic.
  smartthing.run();
}
