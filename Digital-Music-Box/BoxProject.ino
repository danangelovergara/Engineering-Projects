#include "Arduino.h"
#include "SoftwareSerial.h"
#include "DFRobotDFPlayerMini.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- PLAYLIST DATA ---
const int totalSongs = 20;
const char* songNames[totalSongs] = {
  "1. Night We Met" //EXAMPLE
}; //ADD SONG NAMES

// --- PINS ---
const int btnNext   = 2;
const int btnPause  = 3;
const int btnBack   = 4;
const int volumePot = A0;

SoftwareSerial mySoftwareSerial(10, 11); // RX, TX
DFRobotDFPlayerMini myDFPlayer;
LiquidCrystal_I2C lcd(0x27, 16, 2);

int  currentTrack = 1;
bool isPaused     = false;
int  lastVol      = -1;

// Debounce tracking
unsigned long lastNextPress  = 0;
unsigned long lastBackPress  = 0;
unsigned long lastPausePress = 0;
const unsigned long debounceMs = 300;

void setup() {
  mySoftwareSerial.begin(9600);
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Songs for Isa");
  lcd.setCursor(0, 1);
  lcd.print("Loading...");

  if (!myDFPlayer.begin(mySoftwareSerial)) {
    lcd.clear();
    lcd.print("SD Card Error");
    while (true);
  }

  delay(1500); // Give DFPlayer time to fully initialize

  pinMode(btnNext,  INPUT_PULLUP);
  pinMode(btnPause, INPUT_PULLUP);
  pinMode(btnBack,  INPUT_PULLUP);

  // Read pot at startup so Vol never shows -1
  lastVol = map(analogRead(volumePot), 0, 1023, 0, 30);
  myDFPlayer.volume(lastVol);

  myDFPlayer.play(currentTrack);
  updateLCD();
}

void loop() {
  unsigned long now = millis();

  // 1. VOLUME CONTROL (Mapped 0 to 30)
  int potValue   = analogRead(volumePot);
  int currentVol = map(potValue, 0, 1023, 0, 30);
  if (abs(currentVol - lastVol) > 1) {
    myDFPlayer.volume(currentVol);
    lastVol = currentVol;
    // Only rewrite the volume digits to prevent screen flicker
    lcd.setCursor(4, 1);
    lcd.print("  ");
    lcd.setCursor(4, 1);
    lcd.print(lastVol);
  }

  // 2. NEXT BUTTON
  if (digitalRead(btnNext) == LOW && (now - lastNextPress > debounceMs)) {
    lastNextPress = now;
    if (currentTrack >= totalSongs) {
      finishAlbum();
    } else {
      currentTrack++;
      isPaused = false;
      myDFPlayer.play(currentTrack);
      updateLCD();
    }
  }

  // 3. BACK BUTTON — restarts current track if already on track 1
  if (digitalRead(btnBack) == LOW && (now - lastBackPress > debounceMs)) {
    lastBackPress = now;
    if (currentTrack > 1) currentTrack--;
    isPaused = false;
    myDFPlayer.play(currentTrack);
    updateLCD();
  }

  // 4. PAUSE BUTTON
  if (digitalRead(btnPause) == LOW && (now - lastPausePress > debounceMs)) {
    lastPausePress = now;
    isPaused = !isPaused;
    if (isPaused) myDFPlayer.pause();
    else          myDFPlayer.start();
    updateLCD();
  }

  // 5. AUTO-NEXT when a track finishes
  if (myDFPlayer.available() && myDFPlayer.readType() == DFPlayerPlayFinished) {
    if (currentTrack >= totalSongs) {
      finishAlbum();
    } else {
      currentTrack++;
      isPaused = false;
      myDFPlayer.play(currentTrack);
      updateLCD();
    }
  }
}

void updateLCD() {
  lcd.clear();

  // Row 0: song name (all guaranteed <= 16 chars)
  lcd.setCursor(0, 0);
  lcd.print(songNames[currentTrack - 1]);

  // Row 1: volume + status
  lcd.setCursor(0, 1);
  lcd.print("Vol:");
  lcd.print(lastVol);
  if (isPaused) lcd.print(" [PAUSED]");
  else          lcd.print(" Playing");
}

void finishAlbum() {
  myDFPlayer.stop();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Album Finished!");
  lcd.setCursor(0, 1);
  lcd.print("Song's Done! <3");
  delay(5000);
  lcd.noBacklight();
  lcd.clear();
  while (true); // Stays off until power cycled
}
