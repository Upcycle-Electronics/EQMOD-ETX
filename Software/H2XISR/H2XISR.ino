/*
 * Copyright 2017, 2018 John Archbold
*/

// Pin definitions for HBX interface
// =================================
#define HDA1            8           // Pin2, 4, 6 on HBX interface
#define HDA2            10           // Not used
#define HCL1            2           // Pin3 on HBX interface
#define HCL2            3           // Pin5 on HBX interface   

#define H2X_INPUTPU     INPUT_PULLUP  // Set pin data input mode
#define H2X_INPUT       INPUT         // Set pin data input mode
#define H2X_OUTPUT      OUTPUT        // Set pin data output

// Jumpers to run monitor or test
// ==============================
#define TESTHBX         9           // Mega2560 D2
#define MONITORHBX      11          // Mega2560 D3


#define CR              0x0d
#define LF              0x0a
#define H2XLEN          256
#define H2XMASK         H2XLEN - 1

// ETX ISR States
#define START         0x01
#define ACK           0x02
#define COMMAND       0x03
#define DATA          0x04

// ETX Known Commands
#define RotateSlow      0x00          // Output "8.16"    speed
#define RotateFast      0x01          // Output "16.8"    ? speed
#define SetOffset       0x02          // Output "16"      correction offset
#define SetLEDI         0x03          // Output "8"       LED current
#define CalibrateLED    0x04          // None
#define Stop            0x05          // None
#define SlewReverse     0x06          // None
#define SlewForward     0x07          // None
#define GetStatus       0x08          // Input "16.8.1"   ticks.pwm.error
#define GetLEDI         0x09          // Input "8"        LED current
#define GetMotorType    0x0B          // Input "8"        Motor type
#define ResetH2X        0xE4          // None

unsigned  long          timeout;

char BitCountArray[16] =
  { 24,24,16,8,0,0,0,0,25,8,8,0,0,0,0,0 };
                    
volatile unsigned char isr_state;
volatile unsigned long isr_timeout;
volatile unsigned char sFlag;
volatile unsigned char BitCountIndex;
volatile unsigned char HexData;
volatile unsigned char HBXCmnd;
volatile unsigned char HBXData;
volatile unsigned char HBXByteCount;
volatile unsigned char HBXBitCount;

volatile unsigned char H2XRxBuffer[H2XLEN];    // Hold data from H2X
volatile unsigned char H2XRxiPtr = 0;          // Pointer for input from H2X
volatile unsigned char H2XRxoPtr = 0;          // Pointer for output from H2X Rx buffer


void setup() {

  Serial.begin(115200);
  Serial.println("H2X-Monitor");
  attachInterrupt(digitalPinToInterrupt(HCL1), hcl1_isr, FALLING);
  Serial.print ("Az on Pin ");
  Serial.println(HCL1);
  attachInterrupt(digitalPinToInterrupt(HCL2), hcl2_isr, FALLING);
  Serial.print ("Alt on Pin ");
  Serial.println(HCL2);

  HBXMonitorMode();
  sFlag = 0;
  isr_state = START;
  isr_timeout = micros();
  interrupts();
}

void loop() {

  if (H2XRxoPtr != H2XRxiPtr) {
    Serial.write(H2XRxBuffer[H2XRxoPtr]);
    H2XRxoPtr++;
    H2XRxoPtr &= H2XMASK;
  }
}

void HBXMonitorMode(void) {
  HDAListen();                    // HDA as input
  HCL1Listen();                   // HCLs as inputs
  HCL2Listen();
}

void hcl1_isr() {
  if ((micros() - isr_timeout) > 5000) 
    isr_state = START;
  isr_timeout = micros();
  if (isr_state == START) {
    HBXBitCount = 8;
    isr_state = COMMAND;
    H2XRxBuffer[H2XRxiPtr] = '1';  // Finish off with a CRLF
    H2XRxiPtr += 1;
    H2XRxiPtr &= H2XMASK; 
    H2XRxBuffer[H2XRxiPtr] = ',';  // Finish off with a CRLF
    H2XRxiPtr += 1;
    H2XRxiPtr &= H2XMASK;
  }
  else if (isr_state == COMMAND) {
    if (HBXBitCount > 0) {               // Read the command     
      HBXCmnd = (HBXCmnd << 1);
      HBXBitCount -= 1;
      if (digitalRead(HDA1) == HIGH)
        HBXCmnd |= 0x01;    
    }
    else {
      HexData = HBXCmnd >> 4;             // Get high nibble
      if (HexData <= 9) HexData += '0';   // convert to hex
      else HexData += '7';
      H2XRxBuffer[H2XRxiPtr] = HexData;   // Put the (hex) data in the buffer
      H2XRxiPtr += 1;
      H2XRxiPtr &= H2XMASK;

      HexData = HBXCmnd & 0x0F;           // Get low nibble
      if (HexData <= 9) HexData += '0';   // convert to hex
      else HexData += '7';
      H2XRxBuffer[H2XRxiPtr] = HexData;   // Put the (hex) data in the buffer      
      H2XRxiPtr += 1;
      H2XRxiPtr &= H2XMASK;

      H2XRxBuffer[H2XRxiPtr] = ',';  // Finish off with a CRLF
      H2XRxiPtr += 1;
      H2XRxiPtr &= H2XMASK;

      BitCountIndex = HBXCmnd & 0x0F;    // Only 11 known commands
      HBXBitCount = BitCountArray[BitCountIndex]; // Get number of data bits
      if (HBXBitCount > 0) {             // Check bitcount associated with command
        isr_state = DATA;
        HBXBitCount--;                   // This is the first data bit
        HBXByteCount = 1;                // ditto
        if (digitalRead(HDA1) == HIGH)      // Read the first data bit
          HBXData |= 0x01;
      }
      else {                        // Nothing to do - all finished - back to start
        isr_state = START;
        H2XRxBuffer[H2XRxiPtr] = CR;  // Finish off with a CRLF
        H2XRxiPtr += 1;
        H2XRxiPtr &= H2XMASK;
        H2XRxBuffer[H2XRxiPtr] = LF;  // Finish off with a CRLF
        H2XRxiPtr += 1;
        H2XRxiPtr &= H2XMASK;
      }
    }
  }
  else if (isr_state == DATA) {
    if (HBXBitCount > 0) {           // Read the data
      HBXData = (HBXData << 1);
      HBXBitCount -= 1;
      HBXByteCount += 1;
      if (digitalRead(HDA1) == HIGH)
        HBXData |= 0x01;
      if ((HBXByteCount == 8) || (HBXBitCount == 0)) {  // Full byte or last bit

        HexData = HBXData >> 4;             // Get high nibble
        if (HexData <= 9) HexData += '0';   // convert to hex
        else HexData += '7';
        H2XRxBuffer[H2XRxiPtr] = HexData;   // Put the (hex) data in the buffer
        H2XRxiPtr += 1;
        H2XRxiPtr &= H2XMASK;
  
        HexData = HBXData & 0x0F;           // Get low nibble
        if (HexData <= 9) HexData += '0';   // convert to hex
        else HexData += '7';
        H2XRxBuffer[H2XRxiPtr] = HexData;   // Put the (hex) data in the buffer      
        H2XRxiPtr += 1;
        H2XRxiPtr &= H2XMASK;

        if (HBXBitCount != 0) {
          H2XRxBuffer[H2XRxiPtr] = ',';  // Finish off with a CRLF
          H2XRxiPtr += 1;
          H2XRxiPtr &= H2XMASK;
        }

        HBXData = 0;                                    // For the last data bit
        HBXByteCount = 0;      
      }
    }
    if (HBXBitCount == 0) {                            // All data received - back to start
      isr_state = START;   
      H2XRxBuffer[H2XRxiPtr] = CR;  // Finish off with a CRLF
      H2XRxiPtr += 1;
      H2XRxiPtr &= H2XMASK;
      H2XRxBuffer[H2XRxiPtr] = LF;  // Finish off with a CRLF
      H2XRxiPtr += 1;
      H2XRxiPtr &= H2XMASK;
    }
  }
}

void hcl2_isr() {
  if ((micros() - isr_timeout) > 5000) 
    isr_state = START;
  isr_timeout = micros();
  if (isr_state == START) {
    HBXBitCount = 8;
    isr_state = COMMAND;
    H2XRxBuffer[H2XRxiPtr] = '2';  // Finish off with a CRLF
    H2XRxiPtr += 1;
    H2XRxiPtr &= H2XMASK; 
    H2XRxBuffer[H2XRxiPtr] = ',';  // Finish off with a CRLF
    H2XRxiPtr += 1;
    H2XRxiPtr &= H2XMASK;
  }
  else if (isr_state == COMMAND) {
    if (HBXBitCount > 0) {               // Read the command     
      HBXCmnd = (HBXCmnd << 1);
      HBXBitCount -= 1;
      if (digitalRead(HDA1) == HIGH)
        HBXCmnd |= 0x01;    
    }
    else {
      HexData = HBXCmnd >> 4;             // Get high nibble
      if (HexData <= 9) HexData += '0';   // convert to hex
      else HexData += '7';
      H2XRxBuffer[H2XRxiPtr] = HexData;   // Put the (hex) data in the buffer
      H2XRxiPtr += 1;
      H2XRxiPtr &= H2XMASK;

      HexData = HBXCmnd & 0x0F;           // Get low nibble
      if (HexData <= 9) HexData += '0';   // convert to hex
      else HexData += '7';
      H2XRxBuffer[H2XRxiPtr] = HexData;   // Put the (hex) data in the buffer      
      H2XRxiPtr += 1;
      H2XRxiPtr &= H2XMASK;

      BitCountIndex = HBXCmnd & 0x0F;    // Only 11 known commands
      HBXBitCount = BitCountArray[BitCountIndex]; // Get number of data bits
      if (HBXBitCount > 0) {             // Check bitcount associated with command
        isr_state = DATA;
        HBXBitCount--;                   // This is the first data bit
        HBXByteCount = 1;                // ditto
        if (digitalRead(HDA1) == HIGH)      // Read the first data bit
          HBXData |= 0x01;
        H2XRxBuffer[H2XRxiPtr] = ',';     // Data to follow
        H2XRxiPtr += 1;
        H2XRxiPtr &= H2XMASK;
      }
      else {                        // Nothing to do - all finished - back to start
        isr_state = START;
        H2XRxBuffer[H2XRxiPtr] = CR;  // Finish off with a CRLF
        H2XRxiPtr += 1;
        H2XRxiPtr &= H2XMASK;
        H2XRxBuffer[H2XRxiPtr] = LF;  // Finish off with a CRLF
        H2XRxiPtr += 1;
        H2XRxiPtr &= H2XMASK;
      }
    }
  }
  else if (isr_state == DATA) {
    if (HBXBitCount > 0) {           // Read the data
      HBXData = (HBXData << 1);
      HBXBitCount -= 1;
      HBXByteCount += 1;
      if (digitalRead(HDA1) == HIGH)
        HBXData |= 0x01;
      if ((HBXByteCount == 8) || (HBXBitCount == 0)) {  // Full byte or last bit

        HexData = HBXData >> 4;             // Get high nibble
        if (HexData <= 9) HexData += '0';   // convert to hex
        else HexData += '7';
        H2XRxBuffer[H2XRxiPtr] = HexData;   // Put the (hex) data in the buffer
        H2XRxiPtr += 1;
        H2XRxiPtr &= H2XMASK;
  
        HexData = HBXData & 0x0F;           // Get low nibble
        if (HexData <= 9) HexData += '0';   // convert to hex
        else HexData += '7';
        H2XRxBuffer[H2XRxiPtr] = HexData;   // Put the (hex) data in the buffer      
        H2XRxiPtr += 1;
        H2XRxiPtr &= H2XMASK;

        if (HBXBitCount != 0) {
          H2XRxBuffer[H2XRxiPtr] = ',';  // Finish off with a CRLF
          H2XRxiPtr += 1;
          H2XRxiPtr &= H2XMASK;
        }

        HBXData = 0;                                    // For the last data bit
        HBXByteCount = 0;      
      }
    }
    if (HBXBitCount == 0) {                            // All data received - back to start
      isr_state = START;   
      H2XRxBuffer[H2XRxiPtr] = CR;  // Finish off with a CRLF
      H2XRxiPtr += 1;
      H2XRxiPtr &= H2XMASK;
      H2XRxBuffer[H2XRxiPtr] = LF;  // Finish off with a CRLF
      H2XRxiPtr += 1;
      H2XRxiPtr &= H2XMASK;
    }
  }
}

// H2X Low level Functions
// -----------------------
void HDAListen() {
  pinMode(HDA1, H2X_INPUT);
//  digitalWrite(HDA1, HIGH);
}
void HDAFloat() {
  pinMode(HDA1, H2X_INPUT);
}
void HDATalk() {
  digitalWrite(HDA1, HIGH);
  pinMode(HDA1, H2X_OUTPUT);
}
void HCL1Listen() {
  pinMode(HCL1, H2X_INPUT);
}
void HCL1Talk() {
  digitalWrite(HCL1, HIGH);
  pinMode(HCL1, H2X_OUTPUT);
}
void HCL2Listen() {
  pinMode(HCL2, H2X_INPUT);
}
void HCL2Talk() {
  digitalWrite(HCL2, HIGH);
  pinMode(HCL2, H2X_OUTPUT);
}

void H2XReset() {
  HCL1Talk();
  HCL2Talk();
  HDATalk();
/*  digitalWrite(HDA1, LOW);
  TimerDelayuS(H2XRESETTIME);
  digitalWrite(HDA1, HIGH);
  TimerDelayuS(H2XRESETTIME);
  HDAListen();
  */
}
