#include <Keypad.h>
#include <EEPROM.h>
#include <SPI.h>
#include "MFRC522.h"
#include "RCSwitch.h"

#define RST_PIN         5 
#define SS_PIN          10

MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance.
MFRC522::MIFARE_Key key;

#define greenLed 4
#define redLed 3
#define blueLed 2


// Led blink without delay
int ledState = HIGH;
long previousMillis = 0;
long interval = 500;

const byte ROWS = 4; //four rows
const byte COLS = 4; //four columns
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {A5, A4, A3, A2};
byte colPins[COLS] = {A1, A0, 8,9};
Keypad theKeypad = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS); 

char lastKey = 'X';
String masterPassword = "#100216ACDC#"; // This allows to unlock without a RFID Key
String keysBuffer = "";

boolean match = false;
boolean programingMode = false;
byte serialRead;

int successRead; // Variable integer to keep if we have Successful Read from Reader

byte cardPwd[4];
byte storedCard[4];   // Stores an ID read from EEPROM
byte readCard[4];           // Stores scanned ID read from RFID Module

int buttonState;             // the current reading from the input pin
int lastButtonState = LOW;   // the previous reading from the input pin
long lastDebounceTime = 0;  // the last time the output pin was toggled
long debounceDelay = 50;    // the debounce time; increase if the output flickers

void setup() {
  
    pinMode(2, OUTPUT); // Leds
    pinMode(3, OUTPUT);
    pinMode(4, OUTPUT);
    pinMode(6, OUTPUT); // Relay
    digitalWrite(6, LOW);
    
    
    // Key
    for (byte i = 0; i < 6; i++) {
          key.keyByte[i] = 0xFF;
    }
    
    SPI.begin();
    mfrc522.PCD_Init();
    mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max); 
    
    ledOperationMode ();

    Serial.begin(9600);
    while (!Serial); 
    
}

void loop() {
  
   
   if(Serial)
   {
     serialRead = Serial.read();
     if(serialRead != (char)-1)
     {
       switch(serialRead){
            case 'w':
                      Serial.println("!!! Starting Wiping EEPROM !!!");
                      for (int x=0; x<1024; x=x+1){ //Loop end of EEPROM address
                        if (EEPROM.read(x) == 0){ //If EEPROM address 0 
                          // do nothing, already clear, go to the next address in order to save time and reduce writes to EEPROM
                        } 
                        else{
                          EEPROM.write(x, 0); // if not write 0, it takes 3.3mS
                        }
                      }
                      Serial.println("!!! Wiped !!!");
                     break;
            case 'p':
                     programingMode = !programingMode;
                     if(programingMode){ ledprogramingMode(); } else { ledOperationMode(); }
                     break;
            case 'e': 
                     break;
       }
     }
   }
   
  keyRead();
 
  successRead = getID();
  if(programingMode==true  )
  {
    if(successRead){
      if ( findID(readCard) ) {
        if(Serial) Serial.println("Removing RFID Tag");
        deleteID(readCard);
      }
      else {
        if(Serial) Serial.println("Adding RFID Tag");
        writeID(readCard);
      }
      ledOperationMode();
      programingMode=false;
    }
  }
  else
  {
    if(successRead){
     if ( findID(readCard) ) {
     
         waitForPassword();
         bool pwdMatch = true;
         for ( int j = 0; j < 4; j++ ) {
            if (cardPwd[j] != keysBuffer[j] )
              pwdMatch = false;
         }
          
         if(pwdMatch)
         {
            ledAccessGranted(true);
         }
         else
           ledAccessDenied(); 
      }
      else
        ledAccessDenied(); 
    }
  }

}

void keyRead()
{
  lastKey = theKeypad.getKey();
  if (lastKey != 'X' && lastKey){
    Serial.println(lastKey);
    if(lastKey == '*')
    {
      lastKey = 'X';
      keysBuffer = "";
    }
     else
     {
       keysBuffer += lastKey;
       if(keysBuffer == masterPassword)
       {
          lastKey = 'X';
          ledAccessGranted(true);
       }       
     }
  } 
}

// Read an ID from EEPROM
void readID( int number ) {
  int start = (number * 8 ) + 6; // Figure out starting position
  
      Serial.println("Stored card: ");
  for ( int i = 0; i < 4; i++ ) { // Loop 4 times to get the 4 Bytes
    storedCard[i] = EEPROM.read(start+i); // Assign values read from EEPROM to array
    Serial.println(storedCard[i], HEX);
  }
  
      Serial.println("Pwd: ");
  for ( int i = 0; i < 4; i++ ) { // Loop 4 times to get the 4 Bytes
    cardPwd[i] = EEPROM.read(start+4+i); // Assign values read from EEPROM to array
    Serial.println(cardPwd[i], DEC);
  }   
}

// Remove ID from EEPROM
void deleteID( byte a[] ) {
  if ( !findID( a ) ) { // Before we delete from the EEPROM, check to see if we have this card!
    ledAccessDenied(); // If not
  }
  else {
    int num = EEPROM.read(0); // Get the numer of used spaces, position 0 stores the number of ID cards
    int slot; // Figure out the slot number of the card
    int start;// = ( num * 4 ) + 6; // Figure out where the next slot starts
    int looping; // The number of times the loop repeats
    int j;
    int count = EEPROM.read(0); // Read the first Byte of EEPROM that stores number of cards
    slot = findIDSLOT( a ); //Figure out the slot number of the card to delete
    start = (slot * 8) + 6;
    looping = ((num - slot) * 8);
    num--; // Decrement the counter by one
    EEPROM.write( 0, num ); // Write the new count to the counter
    for ( j = 0; j < looping; j++ ) { // Loop the card shift times
      EEPROM.write( start+j, EEPROM.read(start+8+j)); // Shift the array values to 4 places earlier in the EEPROM
    }
    for ( int k = 0; k < 8; k++ ) { //Shifting loop
      EEPROM.write( start+j+k, 0);
    }
    ledAccessGranted(false);
  }
}

// Add ID to EEPROM
void writeID( byte a[] ) {
  
  waitForPassword();
  
  if ( !findID( a ) ) { // Before we write to the EEPROM, check to see if we have seen this card before!
    int num = EEPROM.read(0); // Get the number of used spaces, position 0 stores the number of ID cards
    num++; // Increment the counter by one
    int start = ( num * 8 ) + 6; // Figure out where the next slot starts EDITED
    EEPROM.write( 0, num ); // Write the new count to the counter
    for ( int j = 0; j < 4; j++ ) { // Loop 4 times
      EEPROM.write( start+j, a[j] ); // Write the array values to EEPROM in the right position
    }
    
    for ( int j = 0; j < 4; j++ ) { // Loop 4 times
      EEPROM.write( start+4+j, keysBuffer[j] ); // Write password to EEPROM
    }
    
    ledAccessGranted(false);
  }
  else {
    ledAccessDenied();
  }
  
  keysBuffer = "";
}

// Get PICC's UID
int getID() {
  // Getting ready for Reading PICCs
  if ( ! mfrc522.PICC_IsNewCardPresent()) { //If a new PICC placed to RFID reader continue
    return 0;
  }
  if ( ! mfrc522.PICC_ReadCardSerial()) { //Since a PICC placed get Serial and continue
    return 0;
  }
  // There are Mifare PICCs which have 4 byte or 7 byte UID care if you use 7 byte PICC
  // I think we should assume every PICC as they have 4 byte UID
  // Until we support 7 byte PICCs
  if(Serial)Serial.println("Scanned PICC's UID:");
  for (int i = 0; i < 4; i++) {  // 
    readCard[i] = mfrc522.uid.uidByte[i];
    if(Serial)Serial.print(readCard[i], HEX);
  if(Serial)Serial.println("");}
  mfrc522.PICC_HaltA(); // Stop reading
  return 1;
}

// Check Bytes
boolean checkTwo ( byte a[], byte b[] ) {
  if ( a[0] != NULL ) // Make sure there is something in the array first
    match = true; // Assume they match at first
  for ( int k = 0; k < 4; k++ ) { // Loop 4 times
    if ( a[k] != b[k] ) // IF a != b then set match = false, one fails, all fail
      match = false;
  }
  if ( match ) { // Check to see if if match is still true
    return true; // Return true
  }
  else  {
    return false; // Return false
  }
}

// Find ID From EEPROM
boolean findID( byte find[] ) {
  int count = EEPROM.read(0); // Read the first Byte of EEPROM (n. of cards)
  for ( int i = 1; i <= count; i++ ) {  // Loop once for each EEPROM entry
    readID(i); // Read an ID from EEPROM, it is stored in storedCard[4]
    if( checkTwo( find, storedCard ) ) {  // Check to see if the storedCard read from EEPROM
      return true;
      break; // Stop looking we found it
    }
    else {  // If not, return false   
    }
  }
  return false;
}

// Find Slot
int findIDSLOT( byte find[] ) {
  int count = EEPROM.read(0); // Read the first Byte of EEPROM that
  for ( int i = 1; i <= count; i++ ) { // Loop once for each EEPROM entry
    readID(i); // Read an ID from EEPROM, it is stored in storedCard[4]
    if( checkTwo( find, storedCard ) ) { // Check to see if the storedCard read from EEPROM
      // is the same as the find[] ID card passed
      return i; // The slot number of the card
      break; // Stop looking we found it
    }
  }
}

void waitForPassword()
{
  lastKey = 'X';
  keysBuffer = "";
  led(0,0,0,1);
  while(keysBuffer.length() < 4){
    ledBlink(greenLed);
    keyRead();
  } 
}

// Access denied Leds Mode
void ledAccessGranted(bool relay) {
  if(relay)digitalWrite(6, HIGH);
  led(0,1,0,1);
  delay(1300);
  digitalWrite(6, LOW);
  ledOperationMode ();
}

// Access denied Leds Mode
void ledAccessDenied() {
  led(1,0,0,1);
  delay(1800);
  ledOperationMode ();
}

// Programing Cycle Leds Mode
void ledprogramingMode() {
  led(0,0,0,1);
  led(0,1,0,1);
  led(1,0,0,1);
  led(0,0,1,1);
  led(0,0,0,1);
  led(1,1,1,1);
}

// Normal Leds Mode
void ledOperationMode () {
  led(0,0,0,1);
  led(0,0,1,1);
}

void ledBlink(int ln)
{
  unsigned long currentMillis = millis();
 
  if(currentMillis - previousMillis > interval) {
    previousMillis = currentMillis;   

    if (ledState == LOW)
      ledState = HIGH;
    else
      ledState = LOW;

    digitalWrite(ln, ledState);
  }
}

void led(int r, int g, int b, int x)
{
  while(x>0)
  {
    digitalWrite(redLed, r);
    digitalWrite(greenLed, g);
    digitalWrite(blueLed, b);
    delay(200);
    x--;
  }
}
