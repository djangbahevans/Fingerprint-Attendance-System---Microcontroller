#include <Adafruit_Fingerprint.h>
#include "RTClib.h"
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include "MCUFRIEND_kbv.h"
#include <EEPROM.h>

#define RETURN_OK 200

MCUFRIEND_kbv tft;

RTC_DS3231 rtc;
DateTime now;

#define LOWFLASH (defined(__AVR_ATmega328P__) && defined(MCUFRIEND_KBV_H_))

#define WHITE 0x0000  // WHITE
#define YELLOW 0x001F // YELLOW
#define RED 0x07FF    // RED
#define GREEN 0xF81F  // GREEN
#define VIOLET 0xFFE0 // VIOLET
#define BLACK 0xFFFF  // BLACK

DynamicJsonDocument doc(1024);

#define bluetooth Serial1
#define node_mcu Serial2
#define fingerprint Serial3
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerprint);

uint8_t dispState = 0;

void setup()
{
  Serial.begin(9600);
  bluetooth.begin(9600);  // Default rate of the bluetooth module
  node_mcu.begin(115200); // Node mcu
  Serial.println("Starting...");

  //  Initialize screen
  uint16_t ID = tft.readID();
  tft.begin(ID);
  tft.setRotation(3);
  tft.setTextSize(2);

  startRTC();
  // startSDCard();
  startFingerprintSensor();
  Serial.println("Done with fingerprint");

  delay(3000); // Waiting for Node MCU to finish set up
  // myFile = SD.open("wifi.txt");
  char *wifiDetails = getWiFiDetails();
  sendToMCU(wifiDetails);
  // myFile.close();
  Serial.println("Sent password to node");

  // sendBacklogToMCU();
  Serial.println("Good to go");
}

void loop()
{
  // Get instructions from bluetooth (if any)
  char *b_ins = readFromBluetooth();
  if (strcmp(b_ins, "add") == 0)
  {
    dispState = 0;
    handleAddUser();
  }
  else if (strcmp(b_ins, "backlog") == 0)
  {
    // sendBacklogToMCU();
  }

  else if (strcmp(b_ins, "delete") == 0)
  {
    dispState = 0;
    handleDeleteUser();
  }
  else if (strcmp("setWiFi", b_ins) == 0)
  {
    dispState = 0;
    changeWiFi();
  }

  // Continue checking for finger engagement
  if (dispState == 0)
  {
    dispState = 1;
    printToScreen("Place your finger on the sensor", 0, 100, YELLOW);
  }
  takeFingerprint();
}

/**
 * Setup RTC
 */
void startRTC()
{
  if (!rtc.begin())
  {
    printToScreen("RTC initialization failed!", 5, 100, RED);
    Serial.println("RTC initialization failed!");
    while (1)
      ;
  }
  else
  {
    printToScreen("RTC initialized.", 68, 100, GREEN);
    Serial.println("RTC initialized.");
  }
  if (rtc.lostPower())
  {
    Serial.println("RTC lost power, time reset!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
}

/**
 * Starts SD Card module
 */
void startSDCard()
{
  if (!SD.begin(53))
  {
    Serial.println("SD card initialization failed!");
    printToScreen("SD card initialization failed!", 5, 100, RED);
    while (1)
      ;
  }
  Serial.println("SD card initialized.");
}

/**
 * Setup fingerprint sensor
 */
void startFingerprintSensor()
{
  finger.begin(57600);

  if (finger.verifyPassword())
  {
    printToScreen("Fingerprint sensor initialized.", 0, 100, GREEN);
    Serial.println("Fingerprint sensor initialized.");
  }
  else
  {
    printToScreen("Fingerprint sensor initialization failed!", 0, 100, RED);
    Serial.println("Fingerprint sensor initialization failed!");
    while (1)
      ;
  }
}

/**
 * Reads data from bluetooth and streams values into doc
 * Returns doc["ins"]
 */
char *readFromBluetooth()
{
  String json;
  while (bluetooth.available())
  {
    json = bluetooth.readString();
  }
  deserializeJson(doc, json);
  return doc["ins"];
}

/**
 * Adds new user to the db
 */
void handleAddUser()
{
  // char output[128];
  uint8_t id = doc["id"];
  // serializeJson(doc, output);

  // if (sendToMCU(output) == RETURN_OK)
  getFingerprintEnroll(id); // Enroll on sensor
}

/**
 * Starts user deletion process
 */
void handleDeleteUser()
{
  uint8_t id = doc["id"];

  char output[128];
  serializeJson(doc, output);

  // Start server deletion from phone
  uint8_t p = deleteFingerprint(id);
  if (p == FINGERPRINT_OK)
  {
    printToScreen("Successfully deleted user", 0, 100, GREEN);
  }
  else
  {
    printToScreen("Error deleting user", 0, 100, RED);
  }

  // if (sendToMCU(output) == RETURN_OK)
  // {
  //   deleteFingerprint(id);
  //   SD.remove(String(id) + ".txt");
  //   printToScreen("Successfully  deleted user", 0, 100, GREEN);
  // }
  // else
  // {
  //   printToScreen("Error deleting user", 0, 100, RED);
  // }
  delay(1000);
}

/**
 * Checks if finger is on sensor and scans appropriately
 * Sends the data to the internet if data exists on the Node MCU
 */
void takeFingerprint()
{
  char id = getFingerprintID();
  if (id > 0 && id <= 127) // User found
  {
    dispState = 0;
    printToScreen("User found. Data has been recorded", 0, 100, GREEN);
    Serial.print("User Found");
    now = rtc.now();

    // Send data to internet
    char output[128];
    char date[128];
    doc.clear();

    sprintf(date, "%04d%02d%02d%02d%02d", now.year(), now.month(), now.day(), now.hour(), now.minute());
    doc["ins"] = "att";
    doc["id"] = id;
    doc["data"] = date;

    serializeJson(doc, output);
    if (sendToMCU(output) != RETURN_OK)
    {
      // Error save to SD if data not sent to server
    }
  }
  else if (id == FINGERPRINT_NOMATCH)
  {
    Serial.print("Finger not in system");
  }
}

/**
 * Scans finger and returns fingerprint id
 */
char getFingerprintID()
{
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK)
    return -1;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK)
    return -1;

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_NOMATCH)
    return p;
  if (p != FINGERPRINT_OK)
    return -1;

  // found a match!
  Serial.print("Found ID #");
  Serial.print(finger.fingerID);
  Serial.print(" with confidence of ");
  Serial.println(finger.confidence);
  return finger.fingerID;
}

/**
 * Records fingerprint at fingerprint specified id
 * id: Finger id
 */
uint8_t getFingerprintEnroll(uint8_t id)
{
  int p = -1;
  Serial.print("Waiting for valid finger to enroll as #");
  Serial.println(id);
  printToScreen("Please place your finger on the sensor", 0, 100, VIOLET);
  while (p != FINGERPRINT_OK)
  {
    p = finger.getImage();
    switch (p)
    {
    case FINGERPRINT_OK:
      Serial.println("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      Serial.println(".");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      printToScreen("Error! Try again", 60, 100, RED);
      break;
    case FINGERPRINT_IMAGEFAIL:
      Serial.println("Imaging error");
      printToScreen("Error! Try again", 60, 100, RED);
      break;
    default:
      Serial.println("Unknown error");
      printToScreen("Error! Try again", 60, 100, RED);
      break;
    }
  }

  // OK success!

  p = finger.image2Tz(1);
  switch (p)
  {
  case FINGERPRINT_OK:
    Serial.println("Image converted");
    break;
  case FINGERPRINT_IMAGEMESS:
    Serial.println("Image too messy");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  case FINGERPRINT_PACKETRECIEVEERR:
    Serial.println("Communication error");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  case FINGERPRINT_FEATUREFAIL:
    Serial.println("Could not find fingerprint features");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  case FINGERPRINT_INVALIDIMAGE:
    Serial.println("Could not find fingerprint features");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  default:
    Serial.println("Unknown error");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  }

  Serial.println("Remove finger");
  printToScreen("Remove finger", 70, 100, YELLOW);
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER)
  {
    p = finger.getImage();
  }
  Serial.print("ID ");
  Serial.println(id);
  p = -1;
  Serial.println("Place same finger again");
  printToScreen("Place same finger again", 20, 100, YELLOW);
  while (p != FINGERPRINT_OK)
  {
    p = finger.getImage();
    switch (p)
    {
    case FINGERPRINT_OK:
      Serial.println("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      Serial.print(".");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      printToScreen("Error! Try again", 60, 100, RED);
      break;
    case FINGERPRINT_IMAGEFAIL:
      Serial.println("Imaging error");
      printToScreen("Error! Try again", 60, 100, RED);
      break;
    default:
      Serial.println("Unknown error");
      printToScreen("Error! Try again", 60, 100, RED);
      break;
    }
  }

  // OK success!

  p = finger.image2Tz(2);
  switch (p)
  {
  case FINGERPRINT_OK:
    Serial.println("Image converted");
    break;
  case FINGERPRINT_IMAGEMESS:
    Serial.println("Image too messy");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  case FINGERPRINT_PACKETRECIEVEERR:
    Serial.println("Communication error");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  case FINGERPRINT_FEATUREFAIL:
    Serial.println("Could not find fingerprint features");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  case FINGERPRINT_INVALIDIMAGE:
    Serial.println("Could not find fingerprint features");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  default:
    Serial.println("Unknown error");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  }

  // OK converted!
  Serial.print("Creating model for #");
  Serial.println(id);

  p = finger.createModel();
  if (p == FINGERPRINT_OK)
  {
    Serial.println("Prints matched!");
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  }
  else if (p == FINGERPRINT_ENROLLMISMATCH)
  {
    Serial.println("Fingerprints did not match");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  }
  else
  {
    Serial.println("Unknown error");
    return p;
  }

  Serial.print("ID ");
  Serial.println(id);
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK)
  {
    Serial.println("Stored!");
    printToScreen("User has been successfully added", 0, 100, GREEN);
    delay(2000);
    return p;
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  }
  else if (p == FINGERPRINT_BADLOCATION)
  {
    Serial.println("Could not store in that location");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  }
  else if (p == FINGERPRINT_FLASHERR)
  {
    Serial.println("Error writing to flash");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  }
  else
  {
    Serial.println("Unknown error");
    printToScreen("Something went wrong", 40, 100, RED);
    return p;
  }
}

/**
 * Deletes registered data from finger printer sensor
 * id: Fingerprint id to delete
 * Returns result code
 */
uint8_t deleteFingerprint(uint8_t id)
{
  uint8_t p = -1;

  p = finger.deleteModel(id);

  if (p == FINGERPRINT_OK)
  {
    printToScreen("User has been Deleted", 25, 100, RED);
    Serial.println("Deleted!");
    return p;
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
    return p;
  }
  else if (p == FINGERPRINT_BADLOCATION)
  {
    Serial.println("Could not delete in that location");
    return p;
  }
  else if (p == FINGERPRINT_FLASHERR)
  {
    Serial.println("Error writing to flash");
    return p;
  }
  else
  {
    Serial.print("Unknown error: 0x");
    Serial.println(p, HEX);
    return p;
  }
}

/**
 * Sends data to Node mcu and waits for a response
 * Returns int
 */
int sendToMCU(char *data)
{
  node_mcu.print(data);
  while (!node_mcu.available())
    ;

  int returnCode = node_mcu.readString().toInt();
  return returnCode;
}

/**
 * Sends data to Node mcu and waits for a response
 * Returns int
 */
int sendToMCU(String data)
{
  Serial.println("Sending data: " + data);
  node_mcu.println(data);
  while (!node_mcu.available())
  {
    Serial.println("Waiting for Node");
  }
  Serial.println("Getting response");
  String returnedValue = node_mcu.readString();
  Serial.println("Done. Received: " + returnedValue);
  int returnCode = returnedValue.toInt();
  Serial.println("Done. Received: " + returnCode);
  return returnCode;
}

void printToScreen(char *text, int x, int y, int color)
{
  tft.fillScreen(color);
  tft.setCursor(x, y);

  tft.println(text);
}

void changeWiFi()
{
  char output[50];
  serializeJson(doc, output);

  for (uint8_t i = 0; i < ((String)output).length(); i++)
    EEPROM.update(i, output[i]);
}

char *getWiFiDetails()
{
  char wifi[50];
  uint8_t count = 0;

  while (true)
  {
    wifi[count] = EEPROM.read(count);
    if (wifi[count] == 0)
      break;
  }

  Serial.print("WiFi: ");
  Serial.println(wifi);

  return wifi;
}
