// Unknown 433Mhz weather sensor decoder. Untested in the real world.
// http://arduino.cc/forum/index.php/topic,142871.msg1106336.html#msg1106336

// __           ___       ___    ___
//   |         |  |      |  |   |  |
//   |_________|  |______|  |___|  |
//
//   |  Sync      |    1    |  0   |
//   |  9780us    | 4420us  | 2410us
//      9810        4410      2490

// Defines
#define DataBits0 4                                       // Number of data0 bits to expect
#define DataBits1 32                                      // Number of data1 bits to expect
#define allDataBits 36                                    // Number of data sum 0+1 bits to expect
// isrFlags bit numbers
#define F_HAVE_DATA 1                                     // 0=Nothing in read buffer, 1=Data in read buffer
#define F_GOOD_DATA 2                                     // 0=Unverified data, 1=Verified (2 consecutive matching reads)
#define F_CARRY_BIT 3                                     // Bit used to carry over bit shift from one long to the other
#define F_STATE 7                                         // 0=Sync mode, 1=Data mode

#define DEBUG 0

// uno: pin 2 = int.0 or pin 3 = int.1 see
// http://arduino.cc/en/Reference/attachInterrupt
#define ISR_PIN 2
#define ISR_INT 0

// Constants
const unsigned long sync_MIN = 9600;                      // Minimum Sync time in micro seconds
const unsigned long sync_MAX = 9900;

const unsigned long bit1_MIN = 4200;
const unsigned long bit1_MAX = 4600;

const unsigned long bit0_MIN = 2200;
const unsigned long bit0_MAX = 2600;

const unsigned long glitch_Length = 300;                  // Anything below this value is a glitch and will be ignored.

// Interrupt variables
unsigned long fall_Time = 0;                              // Placeholder for microsecond time when last falling edge occured.
unsigned long rise_Time = 0;                              // Placeholder for microsecond time when last rising edge occured.
byte bit_Count = 0;                                       // Bit counter for received bits.
unsigned long build_Buffer[] = {0,0};                     // Placeholder last data packet being received.
volatile unsigned long read_Buffer[] = {0,0};             // Placeholder last full data packet read.
volatile byte isrFlags = 0;                               // Various flag bits

void PinChangeISR(){                                     // Pin 2 (Interrupt 0) service routine
  unsigned long Time = micros();                          // Get current time
  if (digitalRead(ISR_PIN) == LOW) {
// Falling edge
    if (Time > (rise_Time + glitch_Length)) {
// Not a glitch
      Time = micros() - fall_Time;                        // Subtract last falling edge to get pulse time.
      if (bitRead(build_Buffer[1],31) == 1)
        bitSet(isrFlags, F_CARRY_BIT);
      else
        bitClear(isrFlags, F_CARRY_BIT);

      if (bitRead(isrFlags, F_STATE) == 1) {
// Looking for Data
        if ((Time > bit0_MIN) && (Time < bit0_MAX)) {
// 0 bit
          build_Buffer[1] = build_Buffer[1] << 1;
          build_Buffer[0] = build_Buffer[0] << 1;
          if (bitRead(isrFlags,F_CARRY_BIT) == 1)
            bitSet(build_Buffer[0],0);
          bit_Count++;
        }
        else if ((Time > bit1_MIN) && (Time < bit1_MAX)) {
// 1 bit
          build_Buffer[1] = build_Buffer[1] << 1;
          bitSet(build_Buffer[1],0);
          build_Buffer[0] = build_Buffer[0] << 1;
          if (bitRead(isrFlags,F_CARRY_BIT) == 1)
            bitSet(build_Buffer[0],0);
          bit_Count++;
        }
        else {
// Not a 0 or 1 bit so restart data build and check if it's a sync?
          bit_Count = 0;
          build_Buffer[0] = 0;
          build_Buffer[1] = 0;
          bitClear(isrFlags, F_GOOD_DATA);                // Signal data reads dont' match
          bitClear(isrFlags, F_STATE);                    // Set looking for Sync mode
          if ((Time > sync_MIN) && (Time < sync_MAX)) {
            // Sync length okay
            bitSet(isrFlags, F_STATE);                    // Set data mode
          }
        }
        if (bit_Count >= allDataBits) {
// All bits arrived
          bitClear(isrFlags, F_GOOD_DATA);                // Assume data reads don't match
          if (build_Buffer[0] == read_Buffer[0]) {
            if (build_Buffer[1] == read_Buffer[1]) 
              bitSet(isrFlags, F_GOOD_DATA);              // Set data reads match
          }
          read_Buffer[0] = build_Buffer[0];
          read_Buffer[1] = build_Buffer[1];
          bitSet(isrFlags, F_HAVE_DATA);                  // Set data available
          bitClear(isrFlags, F_STATE);                    // Set looking for Sync mode
digitalWrite(13,HIGH); // Used for debugging
          build_Buffer[0] = 0;
          build_Buffer[1] = 0;
          bit_Count = 0;
        }
      }
      else {
// Looking for sync
        if ((Time > sync_MIN) && (Time < sync_MAX)) {
// Sync length okay
          build_Buffer[0] = 0;
          build_Buffer[1] = 0;
          bit_Count = 0;
          bitSet(isrFlags, F_STATE);                      // Set data mode
digitalWrite(13,LOW); // Used for debugging
        }
      }
      fall_Time = micros();                               // Store fall time
    }
  }
  else {
// Rising edge
    if (Time > (fall_Time + glitch_Length)) {
      // Not a glitch
      rise_Time = Time;                                   // Store rise time
    }
  }
}


void setup() {
  pinMode(13,OUTPUT); // Used for debugging
  Serial.begin(9600);
  pinMode(ISR_PIN,INPUT);
  attachInterrupt(ISR_INT,PinChangeISR,CHANGE);
  Serial.print("# Pin ");
  Serial.print(ISR_PIN);
  Serial.print(" int ");
  Serial.print(ISR_INT);
  Serial.println(" ISR attached");
}

void loop() {
  if (bitRead(isrFlags,F_GOOD_DATA) == 1) {
    serial_dump();
    delay(100);
  }
}

void serial_dump(void) {
    unsigned long myData0 = 0;
    unsigned long myData1 = 0;

    // We have at least 2 consecutive matching reads
    myData0 = read_Buffer[0]; // first 4 bits
    myData1 = read_Buffer[1]; // rest of 32 bits
    bitClear(isrFlags,F_HAVE_DATA); // Flag we have read the data

if (DEBUG) {
    Serial.print("# 0=");
    dec2binLong(myData0,DataBits0);
    Serial.print(" 1=");
    dec2binLong(myData1,DataBits1);
    Serial.println();
}

    Serial.print("temp=");
    int temp = ( myData1 >> 8 ) & 0xFFF;
    if ( temp & 0x800 ) temp = temp | 0xF000;
    Serial.print(temp / 10.0, 1);

    Serial.print(" humidity=");
    Serial.print( myData1 & 0xFF );

    byte b1 = ( myData1 >> 20 ); // second byte from received packet

    Serial.print(" button=");
    Serial.print( b1 & 0x04);

    Serial.print(" battery=");
    Serial.print( b1 & 0x08);

    Serial.print(" channel=");
    Serial.print( ( b1 & 0x03 ) + 1 );

    Serial.print(" rid=");
    Serial.println( myData1 >> 24 );

    // remote but so we don't see this packet again
    bitClear(isrFlags, F_GOOD_DATA);

}

void dec2binLong(unsigned long myNum, byte NumberOfBits) {
  if (NumberOfBits <= 32){
    myNum = myNum << (32 - NumberOfBits);
    for (int i=0; i<NumberOfBits; i++) {
      if (bitRead(myNum,31) == 1) 
      Serial.print("1");
      else 
      Serial.print("0");
      myNum = myNum << 1;
      if ( i % 4 == 3 ) Serial.print(" ");
    }
  }
}
