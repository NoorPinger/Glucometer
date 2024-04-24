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

bool menu = false;
bool home = true;
bool freshStart = true;
bool shiftFlag = false;
bool power = false;
char data[5][10];
int data_index = 0;

void initCharArray()
{
  for(int i = 0; i < 5; i++)
  {
    for(int j = 0; j < 10; j++)
    {
      data[i][j] = '-';
    }
  }

}

void shiftArray()
{
  for(int i = 0; i < 4; i++)
  {
    for(int j = 0; j < 10; j++)
    {
      data[i][j] = data[i+1][j];
    }
  }
  for(int j = 0; j < 10; j++)
  {
    data[4][j] = '-';
  }

}

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      if(data_index == 5)
      {
        shiftArray();
        data_index = 4;
      }


      if (value.length() > 0) {
        for (int i = 0; i < value.length(); i++)
          data[data_index][i] = value[i];
        data_index++;
      }
    }
};

void pb1pressed() {
  menu ^= true;
  home = !menu;
}

void powerPressed() {
  detachInterrupt(digitalPinToInterrupt(POWER_BUTTON));
  power = true;
  menu = false;
  home = false;
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

  pCharacteristic->setValue("Hello World");
  pService->start();

  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->start();
}
void setup() {
  if(freshStart)
  {
    initCharArray();
    freshStart = false;
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
    if(data[i][j] != '-')
    {
      Serial.printf("[%d]: ", i+1);
      for(j = 0; j < 10; j++)
      {
        if(data[i][j] != '-')
          Serial.print(data[i][j]);
      }
      Serial.println();
    }
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
  if(menu)
  {
    printContents();
    delay(500);
  }
  else if(power)
  {
    Serial.println("Power Off");
    gpio_wakeup_enable(GPIO_NUM_2, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    delay(500);
    Serial.flush();
    esp_light_sleep_start();
    
    gpio_wakeup_disable(GPIO_NUM_2);
    home = true;
    menu = false;
    power = false;
    initBLE();
    attachInterrupt(digitalPinToInterrupt(POWER_BUTTON), powerPressed, FALLING);
  }
  else if(home)
  {
    Serial.println("Home Screen");
    if(data_index > 0)
      Serial.printf("Recent Value: %s\n", data[data_index-1]);
    delay(500);
  }
}