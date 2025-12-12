#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>

// ---------------- LCD ----------------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------- Buttons ----------------
const int registerBtn = A1; // Register new card
const int deleteBtn   = A2; // Delete card

// ---------------- RGB LED ----------------
const int ledR = 9;
const int ledG = 10;
const int ledB = A0;

// ---------------- Buzzer ----------------
const int buzzer = 8;

// ---------------- Servo Lock ----------------
const int servoPin = 4;
Servo lockServo;

// ---------------- RFID ----------------
#define RST_PIN 2
#define SS_PIN 3
MFRC522 rfid(SS_PIN, RST_PIN);

// ---------------- State ----------------
bool doorOpen = false; // track door state
#define MAX_CARDS 10
byte registeredUIDs[MAX_CARDS][10]; // store UIDs
byte uidSizes[MAX_CARDS];
int cardCount = 0;

// ---------------- Messages ----------------
const char* helloMsgs[18] = {
  "Halloechen mit Oechen", 
  "Moin-jour", 
  "Tuten Gag", 
  "Gruessli Muesli", 
  "Guten Tacho", 
  "Servus, Haselnuss", 
  "Aloha", 
  "Alles cool im Pool?",
  "Halloechen! Schoen, dich zu sehen!", 
  "Moin Moin, Sonnenschein!",
  "Moin Meister", 
  "Heyho! Bereit fuer Action?", 
  "Willkommen zurueck, Held!", 
  "Na, alles fit bei dir?", 
  "Gruess dich! Bereit zum Staunen?", 
  "Du schon wieder?", 
  "Hi, mutiger Besucher!", 
  "Hallihallo! Auf geht's!"
};

const char* goodbyeMsgs[18] = {
  "Ciao mit Au", 
  "Auf Wiesbaden", 
  "Bundesgartenciao", 
  "Tschuessikowski", 
  "See you later, alligator", 
  "Bis Baldrian", 
  "Auf Wiedersehen, Hawaii", 
  "Hasta la Pasta", 
  "Ciao, Frikadella!",
  "Bis spaeter, Champion!", 
  "Mach's gut und pass auf!", 
  "Auf Wiedersehen, Abenteurer!", 
  "Ciao Kakao!", 
  "Bis dann, Heldenhaft!", 
  "Adieu und viel Spass!", 
  "Bis bald, Weltentdecker!", 
  "Hau rein und tschoe!", 
  "Tschoe mit oe"
};

const char* defaultMsg = "Tscholls Zimmer";

// ---------------- Helpers ----------------
void setColor(int r, int g, int b) {
  analogWrite(ledR, r);
  analogWrite(ledG, g);
  analogWrite(ledB, b);
}

// Buzzer with tone
void beep(int freq = 1000, int duration = 200) {
  tone(buzzer, freq, duration);
  delay(duration);
  noTone(buzzer);
}

// Smooth servo movement (start -> end in small steps)
void smoothServoMove(int startPos, int endPos, int stepDelay = 15) {
  if (startPos < endPos) {
    for (int pos = startPos; pos <= endPos; pos++) {
      lockServo.write(pos);
      delay(stepDelay);
    }
  } else {
    for (int pos = startPos; pos >= endPos; pos--) {
      lockServo.write(pos);
      delay(stepDelay);
    }
  }
}


bool uidMatch(byte* uid, byte size, int &index) {
  for(int i=0; i<cardCount; i++){
    if(uidSizes[i] == size){
      bool match = true;
      for(int j=0; j<size; j++){
        if(registeredUIDs[i][j] != uid[j]){
          match = false;
          break;
        }
      }
      if(match){ index = i; return true; }
    }
  }
  return false;
}

void storeUID(byte* uid, byte size) {
  if(cardCount < MAX_CARDS){
    uidSizes[cardCount] = size;
    for(int i=0; i<size; i++) registeredUIDs[cardCount][i] = uid[i];
    Serial.print("Stored UID: ");
    for(int i=0;i<size;i++){ Serial.print(uid[i], HEX); Serial.print(" "); }
    Serial.println();
    cardCount++;
  }
}

void removeUID(int index){
  Serial.print("Removing UID: ");
  for(int i=0;i<uidSizes[index];i++){ Serial.print(registeredUIDs[index][i], HEX); Serial.print(" "); }
  Serial.println();
  for(int i=index; i<cardCount-1; i++){
    uidSizes[i] = uidSizes[i+1];
    for(int j=0;j<uidSizes[i];j++) registeredUIDs[i][j] = registeredUIDs[i+1][j];
  }
  cardCount--;
}

char* randomHello() {
  return helloMsgs[random(0, 17)];
}

char* randomGoodbye() {
  return goodbyeMsgs[random(0, 17)]; 
}


// ---------------- Scroll / Two-line Display ----------------
void displayTwoLineMessage(const char* msg) {
  Serial.println(msg);
  lcd.clear();
  delay(50); // give LCD some time to clear

  // Copy first 16 characters to top line
  char topLine[17] = {0};
  strncpy(topLine, msg, 16);
  lcd.setCursor(0, 0);
  lcd.print(topLine);

  // Copy next 16 characters to bottom line
  char bottomLine[17] = {0};
  if(strlen(msg) > 16) {
    strncpy(bottomLine, msg + 16, 16);
  }
  lcd.setCursor(0, 1);
  lcd.print(bottomLine);
}




// ---------------- Setup ----------------
enum Mode { NORMAL, REGISTER, DELETE };
Mode systemMode = NORMAL;

unsigned long lastMessageTime = 0;
const unsigned long MESSAGE_DURATION = 10000; // 10 seconds
bool messageActive = false;

void setup() {
  lcd.init();
  lcd.backlight();
  
  pinMode(registerBtn, INPUT_PULLUP);
  pinMode(deleteBtn, INPUT_PULLUP);

  pinMode(ledR, OUTPUT);
  pinMode(ledG, OUTPUT);
  pinMode(ledB, OUTPUT);

  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("RFID reader initialized.");

  lockServo.attach(servoPin);
  lockServo.write(45); // initially locked
  lockServo.write(35); // initially locked
  lockServo.write(25); // initially locked
  lockServo.write(15); // initially locked
  lockServo.write(0); // initially locked


  displayTwoLineMessage(defaultMsg);
  setColor(0,0,255);
  beep(500, 500); // startup beep
}

// ---------------- Loop ----------------
void loop() {
  // ---------------- Buttons ----------------
  if(digitalRead(registerBtn) == LOW){
    systemMode = REGISTER;
    displayTwoLineMessage("Registrierung");
    Serial.println("Entered Registration mode");
    beep(1200, 200);
    lastMessageTime = millis();
    messageActive = true;
    delay(300); // debounce
  }
  if(digitalRead(deleteBtn) == LOW){
    systemMode = DELETE;
    displayTwoLineMessage("Deregistrierung");
    Serial.println("Entered Deletion mode");
    beep(1200, 200);
    lastMessageTime = millis();
    messageActive = true;
    delay(300); // debounce
  }

  // ---------------- RFID ----------------
  if(rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()){
    Serial.print("Card detected. UID: ");
    for(int i=0;i<rfid.uid.size;i++){ Serial.print(rfid.uid.uidByte[i], HEX); Serial.print(" "); }
    Serial.println();

    int idx;

    if(systemMode == REGISTER){
      if(!uidMatch(rfid.uid.uidByte, rfid.uid.size, idx)){
        storeUID(rfid.uid.uidByte, rfid.uid.size);
        displayTwoLineMessage("Karte hinzugefuegt!");
        Serial.println("Card successfully registered");
        beep(1200, 200);
      } else {
        displayTwoLineMessage("Karte bereits registriert!");
        Serial.println("Card already registered");
        beep(300, 200);
      }
      lastMessageTime = millis();
      messageActive = true;
      systemMode = NORMAL;
    }
    else if(systemMode == DELETE){
      if(uidMatch(rfid.uid.uidByte, rfid.uid.size, idx)){
        removeUID(idx);
        displayTwoLineMessage("Karte entfernt!");
        Serial.println("Card removed from system");
        beep(1200, 200);
      } else {
        displayTwoLineMessage("Karte nicht gefunden!");
        Serial.println("Card not found in system");
        beep(300, 200);
      }
      lastMessageTime = millis();
      messageActive = true;
      systemMode = NORMAL;
    }
    else { // NORMAL mode
      if(uidMatch(rfid.uid.uidByte, rfid.uid.size, idx)){
        Serial.println("Authorized card detected");
        if(doorOpen){
          Serial.println("Closing door...");
          for(int i=0;i<3;i++){ setColor(255,0,0); delay(200); setColor(0,0,0); delay(200);}
          smoothServoMove(90,0,5);
          doorOpen = false;
          displayTwoLineMessage(randomGoodbye());
          setColor(255,0,0);
          beep(2000,500);
        } else {
          Serial.println("Opening door...");
          for(int i=0;i<3;i++){ setColor(0,255,0); delay(200); setColor(0,0,0); delay(200);}
          smoothServoMove(0,90,5);
          doorOpen = true;
          displayTwoLineMessage(randomHello());
          setColor(0,255,0);
          beep(1500,500);
        }
        lastMessageTime = millis();
        messageActive = true;
      } else {
        Serial.println("Unauthorized card detected!");
        for(int i=0;i<3;i++){ setColor(255,0,0); delay(200); setColor(0,0,0); delay(200); beep(300,150);}
        displayTwoLineMessage("Karte nicht registriert!");
        lastMessageTime = millis();
        messageActive = true;
        if(doorOpen){
          setColor(0,255,0);
        }else{
          setColor(255,0,0);
        }
      }
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    delay(1000);
  }

  // ---------------- Reset to default message ----------------
  if(messageActive && millis() - lastMessageTime > MESSAGE_DURATION){
      displayTwoLineMessage(defaultMsg);
      messageActive = false;
  }
}