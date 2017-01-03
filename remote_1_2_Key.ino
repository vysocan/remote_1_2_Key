// Remote node for RS485, iButton, and LM335 temp.
// Board v.1.10
//
// ATMEL ATMEGA168 / ARDUINO
//
//                  +-\/-+
//            PC6  1|    |28  PC5 (AI 5)
//      (D 0) PD0  2|    |27  PC4 (AI 4)
//      (D 1) PD1  3|    |26  PC3 (AI 3)
//      (D 2) PD2  4|    |25  PC2 (AI 2)
// PWM+ (D 3) PD3  5|    |24  PC1 (AI 1)
//      (D 4) PD4  6|    |23  PC0 (AI 0)
//            VCC  7|    |22  GND
//            GND  8|    |21  AREF
//            PB6  9|    |20  AVCC
//            PB7 10|    |19  PB5 (D 13)
// PWM+ (D 5) PD5 11|    |18  PB4 (D 12)
// PWM+ (D 6) PD6 12|    |17  PB3 (D 11) PWM
//      (D 7) PD7 13|    |16  PB2 (D 10) PWM
//      (D 8) PB0 14|    |15  PB1 (D 9)  PWM
//                  +----+

#define NOTE_A3  220
#define NOTE_AS3 233
#define NOTE_B3  247
#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466

#include <OneWire.h>
#include <LibRS485.h>
#include <avr/eeprom.h> // Global configuration for in chip EEPROM


#define MY_ADDRESS 1    // 0 is gateway, 15 is multicast
#define LED_GREEN  5    // iButton probe LED
#define LED_RED    4    // iButton probe LED
#define SPEAKER    7    // Speaker pin
#define DE         3    // RS 485 DE pin
#define VERSION    120  // Version of EEPROM struct
#define REG_LEN    21   // size of one conf. element
#define REG_REPEAT 10   // repeat sending

OneWire ds(6);          // Dallas reader on pin with  4k7 pull-up rezistor
RS485_msg msg; 

// Global variables
int8_t  i;
uint8_t addr[8];             // Dallas chip
uint8_t mode = 0;
uint8_t pos;
uint8_t repeat = 0;
uint8_t version = 0;
long    previousMillis = 0;
long    readerMillis = 0;
long    tempMillis = 0;

// Notes and LEDs patterns
char *goodkey  = "G1,,G5,,g0,.";
char *wrongkey = "R1,,R1,,r0,.";
char *auth0    = "R5,r0,.";
char *auth1    = "R5,r0,,,,.";
char *auth2    = "R5,r0,,,,,,.";
char *auth3    = "R5,r0,,,,,,,,.";
char *p        = ".";
char *ok       = "G,g,,,,,,,,,.";
char *armed    = "R,r,,,,,,,,,.";
char *arming   = "R7,r5,R7,r0,,,,,,,,.";
int notes[] = { NOTE_A3, NOTE_B3, NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4, NOTE_G4, NOTE_A4 };

struct config_t {
  uint16_t version;
  char     reg[REG_LEN * 1];
} conf; 

// Float conversion 
union u_tag {
    uint8_t  b[4]; 
    float    fval;
} u;


// Registration 
void send_conf(){ 
  delay(MY_ADDRESS*100); // Wait some time to avoid contention
  msg.address = 0;
  msg.ctrl = FLAG_DTA;
  msg.data_length = REG_LEN + 1; // Add 'R'
  pos = 0;
  do {
    msg.buffer[0] = 'R'; // Registration flag
    for (uint8_t ii=0; ii < REG_LEN; ii++){ msg.buffer[1+ii] = conf.reg[pos+ii]; }
    repeat = 0;
    do {
      i = RS485.msg_write(&msg);
      tone(SPEAKER, notes[pos]);  delay(100); noTone(SPEAKER); delay(100);
      repeat++;
    } while ((i < 0) && (repeat < REG_REPEAT));
    pos =+ REG_LEN;
  } while (pos < sizeof(conf.reg));
  msg.buffer[0] = 0; msg.data_length = 0; // Clear buffer
}

// Set defaults on first time
void setDefault(){
  conf.version = VERSION;
  conf.reg[0]  = 'K';       // Key
  conf.reg[1]  = 'i';       // iButton
  conf.reg[2]  = 0;         // Local address
  conf.reg[3]  = B00000000; // Default setting
  conf.reg[4]  = B00011110; // Default setting, group=16, disabled
  for (uint8_t ii=0; ii < 17; ii++){ conf.reg[5+ii] = 0;} // Placeholder for name
}

void setup() {
  pinMode(DE, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);  
  
  RS485.begin(19200, MY_ADDRESS);

  eeprom_read_block((void*)&conf, (void*)0, sizeof(conf)); // Read current configuration
  if (conf.version != VERSION) setDefault();
  
  delay(1000);
  send_conf(); 
 
  previousMillis = millis();
  readerMillis   = millis(); 
  tempMillis     = millis(); 
}

void loop() {
  // Look for incomming transmissions
  if (RS485.msg_read(&msg) > 0) {
    // Commands from gateway
    if (msg.ctrl == FLAG_CMD) {
      if (msg.data_length == 1)  send_conf(); // Request for registration
      if ((msg.data_length >= 10) && (msg.data_length <= 20)) mode = msg.data_length; // Auth. commands
    }
    // Configuration change 
    if (msg.ctrl == FLAG_DTA && msg.buffer[0]=='R') {
      // Replace part of conf string with new paramters.
      pos = 0; 
      while (((conf.reg[pos] != msg.buffer[1]) || (conf.reg[pos+1] != msg.buffer[2]) || (conf.reg[pos+2] != msg.buffer[3])) && (pos < sizeof(conf.reg))) {
        pos += REG_LEN; // size of one conf. element
      }      
      if (pos < sizeof(conf.reg)) {
        for (uint8_t ii=0; ii < msg.data_length-1; ii++){ conf.reg[pos+ii]=msg.buffer[1+ii]; }
        // Save it to EEPROM
        conf.version = VERSION;
        eeprom_update_block((const void*)&conf, (void*)0, sizeof(conf)); // Save current configuration
        // Send this piece back for re-registration
        msg.address = 0; msg.ctrl = FLAG_DTA;
        repeat = 0;
        do {
          i = RS485.msg_write(&msg);
          tone(SPEAKER, notes[pos]);  delay(100); noTone(SPEAKER); delay(100);
          repeat++;
        } while ((i < 0) && (repeat < REG_REPEAT));
        msg.buffer[0] = 0; msg.data_length = 0; // Clear buffer
      }
    }
  }
  
  // Tone and leds
  if ((long)(millis() - previousMillis) >= 200) {
    previousMillis = millis();   
    if (*p == '.') {
      // reset all sound and LED
      digitalWrite(LED_RED, LOW);
      digitalWrite(LED_GREEN, LOW);
      noTone(SPEAKER);
      // change the mode
      switch (mode) {
        case 10: p = arming; break;
        case 11: p = auth0; break;
        case 12: p = auth1; break;
        case 13: p = auth2; break;
        case 14: p = auth3; break;          
        case 15: p = armed; break;
        default: p = ok; break; // Case 20
      }
    } 
    while (*p != ',') {
      switch (*p) {
        case 'G': digitalWrite(LED_GREEN, HIGH); break;
        case 'g': digitalWrite(LED_GREEN, LOW); break;
        case 'R': digitalWrite(LED_RED, HIGH); break;
        case 'r': digitalWrite(LED_RED, LOW); break;
        case '1'...'9': tone(SPEAKER, notes[*p-49]); break;
        case '0': noTone(SPEAKER); break;  
        default: break;
      }
      p++;  
    }
    p++;
    // check iButton between notes but only every 3000 ms
    if ((unsigned long)(millis() - readerMillis) > 3000) {
      if ( !ds.search(addr)) {
        ds.reset_search();
      } else { // we have chip at reader
        readerMillis = millis();
        // Check of Ibutton crc
        if ( OneWire::crc8( addr, 7) == addr[7]) { // valid crc, send to master
          msg.address = 0;
          msg.ctrl = FLAG_DTA;
          msg.data_length = 8;
          memcpy(msg.buffer, addr, msg.data_length);
          i = RS485.msg_write(&msg);
          p = goodkey; // play 
          ds.reset_search();
        } else { // crc not valid
          p = wrongkey; // play 
          ds.reset_search();
        }
      }
    } // End iButton
  } 
    
} // End main loop
