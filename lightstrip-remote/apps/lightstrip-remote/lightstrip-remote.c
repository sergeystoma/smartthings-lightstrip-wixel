#include <wixel.h>
#include <usb.h>
#include <usb_com.h>
#include <radio_queue.h>
#include <gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uart1.h>

#define P1_0 10 // Strip SDI pin
#define P1_1 11 // Strip CLK pin

/////////////////////////////////////////////////////////////////////////////////
//
//  Globals
//

uint8 _masterUnit = 0;
uint32 _lastBroadcast = 0;

/////////////////////////////////////////////////////////////////////////////////
//
//  Utility
//

// Generate RGB uint32  color.
#define RGB(r, g, b) ((b << 16) | (g << 8) | (r))

// Generate RGB color from HSV.
// H: Hue in the range of [0, 239] 
// S: Saturation in the range of [0, 255]
//      0 produces least saturated color, so modulating V changes result from black to white.
//      255 is the most saturated color, V allows to change result from black to fully saturated value.
// V: Value in the range [0, 255]
uint32 hsvToRgb(int h, int s, int v) 
{
  int32 i, f;

  int32 m, n, v32 = v;
  
  if ((s < 0) || (s > 255) || (v < 0) || (v > 255))
  {
    return RGB(0L, 0L, 0L);
  }
  if (h < 0 || h > 239) 
  {
    return RGB(255L, 255L, 255L);
  }
  i = h / 40;
  f = h - i * 40;
  if (!(i & 1))
  {
    f = 40 - f;
  }

  m = (v * (255 - s)) / 256;
  n = (v * (255 - s * f / 40)) / 256;
  
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
  return RGB(0L, 0L, 0L);
}

/////////////////////////////////////////////////////////////////////////////////
//
//  LED strip management
//

int32 CODE param_stripLength = 5;

uint32* _stripColors;
int _stripMemorySize;

int _currentH = 0;
int _currentS = 0;
int _currentV = 0;

int _targetH = 0;
int _targetS = 0;
int _targetV = 255;

uint32 _lastColorChange = 0;

void ledInit()
{
  int i;

  _stripMemorySize = sizeof(uint32) * param_stripLength;
  _stripColors = malloc(_stripMemorySize);  
  memset(_stripColors, 0, _stripMemorySize);

  for (i = 0; i <param_stripLength; ++i) 
    _stripColors[i] = 0xc0ffffL;
}

void ledSync()
{
  int led;
  uint8 colorBit;
  uint32 color;
  uint32 mask;

  for (led = 0; led < param_stripLength; led++) 
  {
    color = _stripColors[led];

    for (colorBit = 23; colorBit != 255; colorBit--) 
    {
      setDigitalOutput(P1_1, LOW);

      mask = 1L << colorBit;

      if (color & mask)
        setDigitalOutput(P1_0, HIGH);  
      else
        setDigitalOutput(P1_0, LOW);
      
      setDigitalOutput(P1_1, HIGH);
    }
  }

  setDigitalOutput(P1_1, LOW);
}

void ledSetColorTarget(int h, int s, int v)
{
  if (h > 239)
    h = 239;

  _targetH = h;
  _targetS = s;
  _targetV = v;

  _lastBroadcast = 0;
}

void ledWrite()
{
  uint32 color;
  int i;

  color = hsvToRgb(_currentH, _currentS, _currentV);

  for (i = 0; i < param_stripLength; ++i) {          
    _stripColors[i] = color;
  }
}

void ledUpdate()
{
  uint32 now;  

  usbShowStatusWithGreenLed();

  now = getMs();

  if (now - _lastColorChange > 4) 
  {
    _lastColorChange = now;

    if (_currentH != _targetH || _currentS != _targetS || _currentV != _targetV) 
    {
#define UPDATE_COLOR(x, y, step) if (x < y) { x += step; if (x > y) x = y; } else if (x > y) { x -= step; if (x < y) x = y; }
   
      UPDATE_COLOR(_currentH, _targetH, 1);
      UPDATE_COLOR(_currentS, _targetS, 1);
      UPDATE_COLOR(_currentV, _targetV, 1);

      ledWrite();
      ledSync();
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////
//
//  Command parsing communication
//

uint8 hexParse(uint8 ch) 
{
  if (ch >= (uint8)'0' && ch <= (uint8)'9')
    return ch - (uint8)'0';  
  if (ch >= (uint8)'A' && ch <= (uint8)'F')
    return ch - (uint8)'A' + 10;  
  if (ch >= (uint8)'a' && ch <= (uint8)'f')
    return ch - (uint8)'a' + 10;
  return 0;
}

uint8 toHex(uint8 v) 
{
  if (v > 9) 
    return (uint8)'a' + v - 10;

  return (uint8)'0' + v;
}

void commandParse(uint8* buffer)
{
  switch (buffer[0])
  {
    case (uint8)'S':      
      ledSetColorTarget(
        ((int)hexParse(buffer[1]) << 4) | (int)hexParse(buffer[2]),
        ((int)hexParse(buffer[3]) << 4) | (int)hexParse(buffer[4]),
        ((int)hexParse(buffer[5]) << 4) | (int)hexParse(buffer[6]));
      break;
  }
}

/////////////////////////////////////////////////////////////////////////////////
//
//  Master/slave management.
//

void masterUpdate() 
{
  uint8 XDATA* packet;
  uint32 now;

  if (!_masterUnit)
    return;

  now = getMs();

  if (_lastBroadcast == 0 || now - _lastBroadcast > 5 * 1000) 
  {
    _lastBroadcast = now;

    packet = radioQueueTxCurrentPacket();
    if (packet != 0)
    {
      LED_RED(!LED_RED_STATE);

      packet[0] = 6;   // must not exceed RADIO_QUEUE_PAYLOAD_SIZE

      packet[1] = 'W';      
      packet[2] = 'x';      
      packet[3] = 'S';      
      packet[4] = _targetH;
      packet[5] = _targetS;
      packet[6] = _targetV;
      
      radioQueueTxSendPacket();
    }
  }
}

void slaveUpdate()
{
  uint8 XDATA* packet;

  if (_masterUnit) 
    return;

  packet = radioQueueRxCurrentPacket();
  if (packet)
  {
    LED_YELLOW(!LED_YELLOW_STATE);

    if (packet[0] == 6 && packet[1] == (uint8)'W' && packet[2] == (uint8)'x' && packet[3] == (uint8)'S')
    {
      ledSetColorTarget(packet[4], packet[5], packet[6]);
    }

    radioQueueRxDoneWithPacket();
  }
}

/////////////////////////////////////////////////////////////////////////////////
//
//  Arduino communication
//

#define AR_STATE_READ_WAIT 1
#define AR_STATE_READ_SIGNATURE1 2
#define AR_STATE_READ_SIGNATURE2 3
#define AR_STATE_READ_BUFFER 4

uint8 _arState = AR_STATE_READ_WAIT;
uint8 _arBuffer[7] = { 0, 0, 0, 0, 0, 0, 0 };
uint8 _arBufferLength = 0;

void arduinoInit()
{
  uart1Init();
  uart1SetBaudRate(9600);  
}

void arduinoService() 
{
  uint8 ch;

  while (uart1RxAvailable())
  {
    ch = uart1RxReceiveByte();
    
    for (;;) 
    {
      switch (_arState) 
      {
        case AR_STATE_READ_WAIT: 
          if (ch == (uint8)'W') 
          {
            _arState = AR_STATE_READ_SIGNATURE1;                  
          }
          break;
        case AR_STATE_READ_SIGNATURE1: 
          if (ch == (uint8)'x') 
          {
            _arState = AR_STATE_READ_SIGNATURE2;                 
          }
          else
          {
            _arState = AR_STATE_READ_WAIT;
            continue;
          }
          break;
        case AR_STATE_READ_SIGNATURE2: 
          if (ch == (uint8)'|') 
          {
            _arState = AR_STATE_READ_BUFFER;
            _arBufferLength = 0;
          }
          else
          {
            _arState = AR_STATE_READ_WAIT;
            continue;
          }
          break;
        case AR_STATE_READ_BUFFER:
          _arBuffer[_arBufferLength++] = ch;
          if (_arBufferLength >= 7)
          {
            _masterUnit = 1;

            _arState = AR_STATE_READ_WAIT;

            commandParse(_arBuffer);
          }
          break;
      }

      break;
    }
  }
}

void main()
{
  systemInit();
  usbInit();
  radioQueueInit();
  ledInit();
  ledWrite();
  ledSync();
  arduinoInit();

  while(1)
  {
    boardService();
    usbComService();
    arduinoService();        
    ledUpdate();
    masterUpdate();
    slaveUpdate();
  }
}
