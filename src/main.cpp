#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <esp_sleep.h>

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID        "4fafc101-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define PB1 D2
#define POWER_BUTTON D0
#define MAX_DATA 5
#define MAX_DATA_LEN 10

/*
  Flags
*/
bool MENU = false;
bool HOME = true;
bool FRESH_START = true;
bool SHIFT_FLAG = false;
bool POWER = false;

/*
  Array to store the data
*/
char data[MAX_DATA][MAX_DATA_LEN];
int data_index = 0;


// This function initializes the array with '-' so we have a way to check if data exists or not
void initCharArray()
{
  for(int i = 0; i < MAX_DATA; i++)
  {
    for(int j = 0; j < MAX_DATA_LEN; j++)
    {
      data[i][j] = '-';
    }
  }

}

// This function shifts the array to the left by 1 (simple bubble sort)
void shiftArray()
{
  for(int i = 0; i < MAX_DATA-1; i++)
  {
    for(int j = 0; j < MAX_DATA_LEN; j++)
    {
      data[i][j] = data[i+1][j];
    }
  }
  for(int j = 0; j < 10; j++)
  {
    data[MAX_DATA-1][j] = '-';
  }

}

/*
  This class is used to handle the BLE characteristics
  It saves the data to the array

  TODO: Add code for time-stamps
*/
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      if(data_index == MAX_DATA)
      {
        shiftArray();
        data_index = MAX_DATA-1;
      }


      if (value.length() > 0) {
        for (int i = 0; i < value.length(); i++)
          data[data_index][i] = value[i];
        data_index++;
      }
    }
};

void pb1pressed() {
  MENU ^= true;
  HOME = !MENU;
}

void powerPressed() {
  detachInterrupt(digitalPinToInterrupt(POWER_BUTTON));
  POWER = true;
  MENU = false;
  HOME = false;
}

void initBLE()
{
  BLEDevice::init("Glucometer");
  BLEServer *pServer = BLEDevice::createServer();

  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE
                                       );

  pCharacteristic->setCallbacks(new MyCallbacks());

  pCharacteristic->setValue("Milligrams per deciliter (mg/dL)");
  pService->start();

  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->start();
}
void setup() {
  if(FRESH_START)
  {
    initCharArray();
    FRESH_START = false;
  }
  pinMode(PB1, INPUT_PULLUP);
  pinMode(POWER_BUTTON, INPUT_PULLDOWN);

  attachInterrupt(digitalPinToInterrupt(PB1), pb1pressed, FALLING);
  attachInterrupt(digitalPinToInterrupt(POWER_BUTTON), powerPressed, RISING);
  Serial.begin(115200);

  initBLE();
}

void printContents()
{
  uint8_t i,j = 0;
  for(int i = 0; i < 5; i++)
  {
    // if(data[i][j] != '-')
    // {
      Serial.printf("[%d]: ", i+1);
      for(j = 0; j < 10; j++)
      {
        if(data[i][j] != '-')
          Serial.print(data[i][j]);
      }
      Serial.println();
    // }
  }
}

void printArray()
{
  for(int i = 0; i < 5; i++)
  {
    for(int j = 0; j < 10; j++)
    {
      Serial.printf("[%d][%d] %c\t",i,j,data[i][j]);
    }
    Serial.println();
  }

}


void loop() 
{
  if(MENU)
  {
    printContents();
    delay(500);
  }
  else if(POWER)
  {
    Serial.println("POWER Off");
    gpio_wakeup_enable(GPIO_NUM_2, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    delay(500);
    Serial.flush();
    esp_light_sleep_start();
    
    gpio_wakeup_disable(GPIO_NUM_2);
    HOME = true;
    MENU = false;
    POWER = false;
    initBLE();
    attachInterrupt(digitalPinToInterrupt(POWER_BUTTON), powerPressed, FALLING);
  }
  else if(HOME)
  {
    Serial.println("HOME Screen");
    if(data_index > 0)
    {
      Serial.print("Recent Value: ");
      for(int i = 0; i < MAX_DATA_LEN; i++)
      {
        if(data[data_index-1][i] != '-')
          Serial.print(data[data_index-1][i]);
      }
      Serial.println();

    }
    delay(500);
  }
}