#include <Arduino.h>
#include <sys/time.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <esp_sleep.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>


// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID        "b7ff2233-3c43-44da-ac1d-fa3075fa3ebc"
#define CHARACTERISTIC_UUID "499d1e04-4a8b-4767-abe9-1bab1a5f4353"
#define MENU_BUTTON D2
#define POWER_BUTTON D0
#define MENU_SELECT D0
#define MENU_UP D3
#define MENU_DOWN D1
#define MAX_DATA 4
#define MAX_DATA_LEN 5


struct timeval start, now;
long seconds;
long seconds_offset = 0;
volatile unsigned long last_interrupt_time_up, last_interrupt_time_down, last_interrupt_time_select = 0;
LiquidCrystal_I2C lcd(0x27, 16, 2);  // set the LCD address to 0x27 for a 16 chars and 2 line display

/*
  Flags
*/
bool MENU = false;
bool HOME = true;
bool FRESH_START = true;
bool SHIFT_FLAG = false;
bool POWER = false;
bool INVALID_DATA = false;
bool setTime = false;
bool MENU_FIRST = false;
bool SELECT = false;
bool DISPLAY_DATA = false;

/*
  Array to store the data
*/
char data[MAX_DATA][MAX_DATA_LEN];
int data_index = 0;
int8_t menu_index = 0;
uint8_t menu_debounce_up, menu_debounce_down = 0;

char str[5];
uint16_t hrCount = 0;
uint16_t  minCount = 0;


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
  for(int j = 0; j < MAX_DATA; j++)
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
      if(setTime)
      {
        if(value.length() > 0)
        {
          seconds_offset = (((int((value[0]))-48)*10 + (int((value[1]))-48)) * 3600) + (((int((value[3]))-48)*10 + (int((value[4])))-48) * 60);
          now.tv_sec = seconds_offset;
          gettimeofday(&start, NULL);
        }
        setTime = false;
        HOME = true;
      }
      else
      {
        if(data_index == MAX_DATA)
        {
          shiftArray();
          data_index = MAX_DATA-1;
        }


        if (value.length() > 0 && value.length() <= MAX_DATA_LEN) {
          for (int i = 0; i < value.length(); i++)
            data[data_index][i] = value[i];
          data_index++;
        }
        else
        {
          INVALID_DATA = true;
        }
      }
    }
};

/*
  This function is called when the menu button is pressed
  It toggles the MENU flag
  This allows us to use only one button to go to the menu and back to the home screen
*/
void menuPressed() {
  menu_index = 0;
  MENU_FIRST = true;
  MENU ^= true;
  HOME = !MENU;
}


void d4Pressed()
{
  setTime = true;
}

void selectPressed()
{
  unsigned long interrupt_time_select = millis();
  if(interrupt_time_select - last_interrupt_time_select > 200)
  {
    SELECT = true;
  }
  last_interrupt_time_select = interrupt_time_select;
}

void menuUpPressed()
{
  // detachInterrupt(digitalPinToInterrupt(MENU_UP));
  unsigned long interrupt_time_up = millis();
  if(interrupt_time_up - last_interrupt_time_up > 200)
  {
    menu_index++;
    if(menu_index > 2)
    {
      menu_index = 0;
    }
  }
  last_interrupt_time_up = interrupt_time_up;
  // delay(10);
}

void menuDownPressed()
{
  // detachInterrupt(digitalPinToInterrupt(MENU_DOWN));
  unsigned long interrupt_time_down = millis();
  if(interrupt_time_down - last_interrupt_time_down > 200)
  {
    menu_index--;
    if(menu_index < 0)
    {
      menu_index = 2;
    }
  }
  last_interrupt_time_down = interrupt_time_down;
  // delay(10);
}


/*
  This function is called when the power button is pressed
  It sets the toggle flag
  We want to use the same button to power on so for that we have to implement debouncing
  This is done by detaching the interrupt and re-attaching it after a delay (in loop)
  All other flags are set to false
*/
void powerPressed() {
  detachInterrupt(digitalPinToInterrupt(POWER_BUTTON));
  POWER = true;
  MENU = false;
  HOME = false;
}

/*
  This function initializes the BLE server
  It sets the service and characteristic UUIDs
  It also sets the value of the characteristic
*/
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
  gettimeofday(&start, NULL);
  lcd.init();                      // initialize the lcd 
  lcd.backlight();
  /*
    Fresh start is used to initialize the array only once
  */
  if(FRESH_START)
  {
    initCharArray();
    FRESH_START = false;
  }

  /*
    Setting up the buttons
    Used pull-down for power as it was working better
  */
  pinMode(MENU_BUTTON, INPUT_PULLUP);
  pinMode(POWER_BUTTON, INPUT_PULLDOWN);
  // pinMode(D4, INPUT_PULLUP);
  pinMode(MENU_UP, INPUT_PULLUP);
  pinMode(MENU_DOWN, INPUT_PULLUP);


  /*
    Attaching the interrupts
  */
  attachInterrupt(digitalPinToInterrupt(MENU_BUTTON), menuPressed, FALLING);
  attachInterrupt(digitalPinToInterrupt(POWER_BUTTON), powerPressed, RISING);
  // attachInterrupt(digitalPinToInterrupt(D4), d4Pressed, FALLING);
  attachInterrupt(digitalPinToInterrupt(MENU_UP), menuUpPressed, FALLING);
  attachInterrupt(digitalPinToInterrupt(MENU_DOWN), menuDownPressed, FALLING);


  Serial.begin(115200);
  initBLE();
}

/*
  This function is called when the MENU flag is set in the interrupt, it prints the contents of the array
*/
void printContents()
{
  uint8_t i,j = 0;
  for(int i = 0; i < MAX_DATA; i++)
  {
      //Serial.printf("[%d]: ", i+1);
      for(j = 0; j < MAX_DATA_LEN; j++)
      {
        if(data[i][j] != '-');
          //Serial.print(data[i][j]);
      }
      //Serial.println();
  }
}

void sleepHandler()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Powering Off");
  //Serial.println("Powering Off");
  delay(500);
  lcd.print(".");
  delay(1000);
  lcd.print(".");
  delay(1000);
  lcd.print(".");
  lcd.noDisplay();
  lcd.noBacklight();
  gpio_wakeup_enable(GPIO_NUM_2, GPIO_INTR_HIGH_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  delay(500);
  //Serial.flush();
  esp_light_sleep_start();
}

void wakeupHandler()
{
  lcd.display();
  lcd.clear();
  gpio_wakeup_disable(GPIO_NUM_2);
  HOME = true;
  MENU = false;
  POWER = false;
  lcd.setCursor(0, 0);
  lcd.backlight();
  lcd.print("Powering On");
  delay(500);
  lcd.print(".");
  delay(1000);
  lcd.print(".");
  delay(1000);
  lcd.print(".");
  lcd.clear();
  // lcd.noBacklight();
  initBLE();
  attachInterrupt(digitalPinToInterrupt(POWER_BUTTON), powerPressed, FALLING);
}


/*
  Debug, was used to confirm contents of the array
*/
void printArray()
{
  for(int i = 0; i < MAX_DATA; i++)
  {
    for(int j = 0; j < MAX_DATA_LEN; j++)
    {
      //Serial.printf("[%d][%d] %c\t",i,j,data[i][j]);
    }
    //Serial.println();
  }

}

void menuScreen()
{
  detachInterrupt(digitalPinToInterrupt(POWER_BUTTON));
  attachInterrupt(digitalPinToInterrupt(MENU_SELECT), selectPressed, FALLING);
  // delay(100);
  // attachInterrupt(digitalPinToInterrupt(MENU_UP), menuUpPressed, FALLING);
  // attachInterrupt(digitalPinToInterrupt(MENU_DOWN), menuDownPressed, FALLING);
  if(MENU_FIRST)
  {
    lcd.clear();
    MENU_FIRST = false;
  }
  // lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Main Menu");
  lcd.setCursor(0, 1);
  switch(menu_index)
  {
    case 0:
      if(seconds % 2 == 0)
        lcd.print("1: View Data");
      else
        lcd.print("1:            ");
      break;
    case 1:
      if(seconds % 2 == 0)
        lcd.print("2: Set Time ");
      else
        lcd.print("2:            ");
      if(SELECT)
      {
        MENU = false;
        MENU_FIRST = true;
        setTime = true;
        SELECT = false;
      }
      break;
    case 2:
      if(seconds % 2 == 0)
        lcd.print("3: Back     ");
      else
        lcd.print("3:            ");
      if(SELECT)
      {
        MENU = false;
        HOME = true;
        SELECT = false;
      }
      break;
  }

}


void getTime()
{
  hrCount = seconds / 3600;
  if(hrCount > 12)
  {
    hrCount -= 12;
  }
  minCount = (seconds % 3600) / 60;
  if(minCount > 59)
  {
    minCount = 0;
  }
}
void loop() 
{
  gettimeofday(&now, NULL);
  now.tv_sec += seconds_offset;
  seconds = (now.tv_sec - start.tv_sec);
  getTime();
  if(seconds >= 86400)
  {
    start = now;
    seconds_offset = 0;
    hrCount = 0;
    minCount = 0;
  }
  if(MENU)
  {
    menuScreen();
    Serial.println("seconds: " + String(seconds));
    Serial.printf("hrCount: %d, minCount: %d\n", hrCount, minCount);
    // printContents();
    // delay(500);
  }
  else if(POWER)
  {
    sleepHandler();
    wakeupHandler();
  }
  else if(HOME)
  {
    detachInterrupt(digitalPinToInterrupt(MENU_SELECT));
    attachInterrupt(digitalPinToInterrupt(POWER_BUTTON), powerPressed, FALLING);
    if(MENU_FIRST)
    {
      lcd.clear();
      MENU_FIRST = false;
    }
    lcd.setCursor(11, 1);
    if(hrCount == 0)
      hrCount = 12;
    if(hrCount < 10)
      lcd.print("0");
    lcd.print(hrCount);
    if(seconds % 2 == 0)
      lcd.print(":");
    else
      lcd.print(" ");
    if(minCount < 10)
      lcd.print("0");
    lcd.print(minCount);
    if(INVALID_DATA)
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Invalid Data");
      INVALID_DATA = false;
      delay(1500);
    }
    else
    {
      if(data_index > 0)
      {
        lcd.setCursor(0, 0);
        lcd.print("Latest Reading:");
        lcd.setCursor(0, 1);
        for(int i = 0; i < MAX_DATA_LEN; i++)
        {
          if(data[data_index-1][i] != '-')
          {
            lcd.write(data[data_index-1][i]);

          }
        }
        lcd.print(" mg/dL  ");

      }
      else
      {
        lcd.setCursor(0, 0);
        lcd.print("Waiting    ");
        lcd.setCursor(0, 1);
        lcd.print("       ");
      }
    }
  }
  else if(setTime)
  {
    if(MENU_FIRST)
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Enter Time:");
      lcd.setCursor(0, 1);
      lcd.print("HH:MM");
      MENU_FIRST = false;
    }
  }
}