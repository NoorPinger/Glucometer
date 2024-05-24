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

#define MENU_BUTTON D1
#define POWER_BUTTON D0
#define MENU_SELECT D4
#define MENU_UP D2
#define MENU_DOWN D3
#define MAX_DATA 5
#define MAX_DATA_LEN 5
#define TIMEOUT_SECONDS 30

/*
  Struct to store the time stamps
*/
typedef struct {
  uint8_t hours;
  uint8_t minutes;
} time_stamps;

time_stamps time_stamps_array[MAX_DATA];

struct timeval start, now;
struct timeval epoch = {0, 0};


char data[MAX_DATA][MAX_DATA_LEN];    // Array to store the data
int data_index = 0;                   // Index to keep track of the data
int8_t menu_index = 0;                // Index to control menu scrolling on the screen
int16_t saved_data_index = 0;         // Index to control saved data scrolling on the screen
uint16_t hrCount = 0;                 // Variable to store the hours elapsed
uint16_t  minCount = 0;               // Variable to store the minutes elapsed
hw_timer_t * timer = NULL;            // Timer used for screen timeout
long seconds;                         // Variable to store the seconds elapsed
long seconds_offset = 0;              // Variable to change the initial seconds once time is set by the user

/*
  used to debounce the buttons
*/
volatile unsigned long last_interrupt_time_up, last_interrupt_time_down, last_interrupt_time_select, last_interrupt_time_menu = 0;

/*
  LCD object
*/
LiquidCrystal_I2C lcd(0x27, 16, 2);  // set the LCD address to 0x27 for a 16 chars and 2 line display

/*
  Flags
*/
bool           MENU = false;              // Flag to display the menu
bool           HOME =  true;              // Flag to display the home screen
bool    FRESH_START =  true;              // Flag to initialize the array only once and display the start screen
bool     SHIFT_FLAG = false;              // Flag to shift the array
bool          POWER = false;              // Flag to power off the device
bool   INVALID_DATA = false;              // Flag to display invalid data
bool       SET_TIME = false;              // Flag to set the time
bool     MENU_FIRST = false;              // Flag used to display unnecasry data only once
                                          // example: text "Main Menu" needs to be displayed only once
bool         SELECT = false;              // Flag to select the menu item
bool   DISPLAY_DATA = false;              // Flag to display the saved data
bool    MENU_SCROLL = false;              // Flag to scroll the menu
bool DISPLAY_SCROLL = false;              // Flag to scroll the saved data
bool        TIMEOUT = false;              // Flag to turn off the screen
bool TIMEOUT_WAKEUP =  true;              // Flag to turn on the screen



// This function initializes the array with '-' so we have a way to check if data exists or not
void initCharArray()
{
  data_index = 0;
  for(int i = 0; i < MAX_DATA; i++)
  {
    time_stamps_array[i].hours = '-';
    time_stamps_array[i].minutes = '-';
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
    time_stamps_array[i].hours = time_stamps_array[i+1].hours;
    time_stamps_array[i].minutes = time_stamps_array[i+1].minutes;
  }
  for(int j = 0; j < MAX_DATA; j++)
  {
    data[MAX_DATA-1][j] = '-';
  }

  /*
    Setting the last element of the time stamps array to '-'
  */
  time_stamps_array[MAX_DATA-1].hours = '-';
  time_stamps_array[MAX_DATA-1].minutes = '-';

}

/*
  This class is used to handle the BLE characteristics
  It saves the data to the array
*/
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();

      /*
        Reset timeout timer
      */
      TIMEOUT = false;
      TIMEOUT_WAKEUP = true;
      timerWrite(timer, 0);

      /*
        If the user wants to set the time, then save the next BLE event as time
        input must be in the format HH:MM
      */
      if(SET_TIME)
      {
        if((value.length() == 5) && (value[2] == ':'))
        {
          seconds_offset = (((int((value[0]))-48)*10 + (int((value[1]))-48)) * 3600) + (((int((value[3]))-48)*10 + (int((value[4])))-48) * 60);
          now.tv_sec = seconds_offset;
          gettimeofday(&start, NULL);
        }
        else
        {
          INVALID_DATA = true;
        }
        SET_TIME = false;
        HOME = true;
      }
      else
      {
        /*
          Else save the data to the array
        */
        
        /*
          If the array is full, then shift the array to the left by 1
        */
        if(data_index == MAX_DATA)
        {
          shiftArray();
          data_index = MAX_DATA-1;
        }

        /*
          If the data is valid, then save it to the array
          TODO: Add more validation checks like if data is only numbers or not
          Also save the time stamps
        */
        if (value.length() > 0 && value.length() <= MAX_DATA_LEN) {
          for (int i = 0; i < value.length(); i++)
            data[data_index][i] = value[i];
          time_stamps_array[data_index].hours = hrCount;
          time_stamps_array[data_index].minutes = minCount;
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
  This function is called when the select button is pressed
  It sets the SELECT flag only when MENU flag is set
  This allows us to use the menu
  This button also resets the timeout timer
*/
void selectPressed()
{
  TIMEOUT = false;
  TIMEOUT_WAKEUP = true;
  timerWrite(timer, 0);
  unsigned long interrupt_time_select = millis();
  if(MENU)            
  {
    if(interrupt_time_select - last_interrupt_time_select > 200)
    {
      SELECT = true;
    }
  }
  last_interrupt_time_select = interrupt_time_select;
}

/*
  This function is called when the menu button is pressed
  It toggles the MENU flag
  This allows us to use only one button to go to the menu and back to the home screen
*/
void menuPressed() 
{
  TIMEOUT = false;
  TIMEOUT_WAKEUP = true;
  timerWrite(timer, 0);
  unsigned long interrupt_time_menu = millis();
  if(interrupt_time_menu - last_interrupt_time_menu > 200)
  {
    SET_TIME = false;
    DISPLAY_DATA = false;
    MENU_SCROLL = true;
    menu_index = 0;
    MENU_FIRST = true;
    MENU ^= true;
    HOME = !MENU;
  }
  last_interrupt_time_menu = interrupt_time_menu;
}




void menuUpPressed()
{
  TIMEOUT = false;
  TIMEOUT_WAKEUP = true;
  timerWrite(timer, 0);
  unsigned long interrupt_time_up = millis();
  if(MENU_SCROLL)
  {
    if(interrupt_time_up - last_interrupt_time_up > 200)
    {
      menu_index++;
      if(menu_index > 3)
      {
        menu_index = 0;
      }
    }
  }
  else if(DISPLAY_SCROLL)
  {
    if(interrupt_time_up - last_interrupt_time_up > 200)
    {
      saved_data_index++;
      if(saved_data_index > MAX_DATA-1)
      {
        saved_data_index = 0;
      }
    }
  }

  last_interrupt_time_up = interrupt_time_up;
}

void menuDownPressed()
{
  TIMEOUT = false;
  TIMEOUT_WAKEUP = true;
  timerWrite(timer, 0);
  unsigned long interrupt_time_down = millis();
  if(MENU_SCROLL)
  {
    if(interrupt_time_down - last_interrupt_time_down > 200)
    {
      menu_index--;
      if(menu_index < 0)
      {
        menu_index = 3;
      }
    }
  }
  else if(DISPLAY_SCROLL)
  {
    if(interrupt_time_down - last_interrupt_time_down > 200)
    {
      saved_data_index--;
      if(saved_data_index < 0)
      {
        saved_data_index = MAX_DATA-1;
      }
    }
  }
  last_interrupt_time_down = interrupt_time_down;
}


/*
  This function is called when the power button is pressed
  It sets the toggle flag
  We want to use the same button to power on so for that we have to implement debouncing
  This is done by detaching the interrupt and re-attaching it after a delay (in loop)
  All other flags are set to false
*/
void powerPressed() 
{
  TIMEOUT = false;
  TIMEOUT_WAKEUP = true;
  timerWrite(timer, 0);
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
  BLEDevice::init("UTA Glucometer");
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

void timerIsr()
{
  TIMEOUT = true;
  TIMEOUT_WAKEUP = false;
}

void displayStartScreen()
{
  lcd.setCursor(0, 0);
    lcd.print("UTA Glucometer");
    lcd.setCursor(0, 1);
    lcd.backlight();
    lcd.print("Powering On");
    delay(500);
    lcd.print(".");
    delay(1000);
    lcd.print(".");
    delay(1000);
    lcd.print(".");
    lcd.clear();
}

void displayShutdownScreen()
{
  lcd.setCursor(0, 0);
  lcd.print("Powering Off");
  delay(500);
  lcd.print(".");
  delay(1000);
  lcd.print(".");
  delay(1000);
  lcd.print(".");
  lcd.noDisplay();
  lcd.noBacklight();
}

void setup() {
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &timerIsr, true);
  timerAlarmWrite(timer, TIMEOUT_SECONDS * 1000000, true);
  gettimeofday(&start, NULL);
  lcd.init();                      // initialize the lcd 
  lcd.backlight();
  /*
    Fresh start is used to initialize the array only once
  */
  if(FRESH_START)
  {
    initCharArray();
    displayStartScreen();
    FRESH_START = false;
  }

  /*
    Setting up the buttons
    Used pull-down for power as it was working better
  */
  pinMode(MENU_BUTTON, INPUT_PULLUP);
  pinMode(POWER_BUTTON, INPUT_PULLDOWN);
  pinMode(MENU_UP, INPUT_PULLUP);
  pinMode(MENU_DOWN, INPUT_PULLUP);


  /*
    Attaching the interrupts
  */
  attachInterrupt(digitalPinToInterrupt(MENU_BUTTON), menuPressed, FALLING);
  attachInterrupt(digitalPinToInterrupt(POWER_BUTTON), powerPressed, RISING);
  attachInterrupt(digitalPinToInterrupt(MENU_SELECT), selectPressed, FALLING);
  attachInterrupt(digitalPinToInterrupt(MENU_UP), menuUpPressed, FALLING);
  attachInterrupt(digitalPinToInterrupt(MENU_DOWN), menuDownPressed, FALLING);


  Serial.begin(115200);
  initBLE();
  timerAlarmEnable(timer);
}

void sleepHandler()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  displayShutdownScreen();
  gpio_wakeup_enable(GPIO_NUM_2, GPIO_INTR_HIGH_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  delay(500);
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
  displayStartScreen();
  initBLE();
  attachInterrupt(digitalPinToInterrupt(POWER_BUTTON), powerPressed, FALLING);
}



void menuScreen()
{
  if(MENU_FIRST)
  {
    lcd.clear();
    MENU_FIRST = false;
  }
  lcd.setCursor(0, 0);
  lcd.print("Main Menu");
  lcd.setCursor(0, 1);
  switch(menu_index)
  {
    case 0:
      lcd.print("1: View Data  ");
      if(SELECT)
      {
        saved_data_index = 0;
        MENU_FIRST = true;
        MENU_SCROLL = false;
        DISPLAY_SCROLL = true;
        MENU = false;
        HOME = false;
        DISPLAY_DATA = true;
        SELECT = false;
      }
      break;
    case 1:
      lcd.print("2: Set Time   ");
      if(SELECT)
      {
        MENU = false;
        MENU_FIRST = true;
        SET_TIME = true;
        SELECT = false;
      }
      break;
    case 2:
      lcd.print("3: Clear Data");
      if(SELECT)
      {
        initCharArray();
        MENU = false;
        HOME = true;
        SELECT = false;
      }
      break;
    case 3: 
      lcd.print("4: Back       ");
      if(SELECT)
      {
        MENU = false;
        HOME = true;
        SELECT = false;
      }
      break;

  }

}

void displaySavedData()
{
  if(MENU_FIRST)
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Saved");
    MENU_FIRST = false;
  }
  lcd.setCursor(11, 0);
  if(time_stamps_array[saved_data_index].hours == 0)
    time_stamps_array[saved_data_index].hours  = 12;
  if(time_stamps_array[saved_data_index].hours == '-')
    lcd.print("--");
  else if(time_stamps_array[saved_data_index].hours < 10)
  {
    lcd.print("0");
    lcd.print(time_stamps_array[saved_data_index].hours);
  }
  else
    lcd.print(time_stamps_array[saved_data_index].hours);
  lcd.print(":");
  if(time_stamps_array[saved_data_index].minutes == '-')
    lcd.print("--");
  else if(time_stamps_array[saved_data_index].minutes < 10)
  {
    lcd.print("0");
    lcd.print(time_stamps_array[saved_data_index].minutes);
  }
  else
    lcd.print(time_stamps_array[saved_data_index].minutes);

  lcd.setCursor(0, 1);
  lcd.printf("[%d]: ", saved_data_index+1);
  for(int j = 0; j < MAX_DATA_LEN; j++)
  {
    if(data[saved_data_index][j] != '-')
    {
      lcd.write(data[saved_data_index][j]);
    }
    else
      lcd.print(" ");
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
    settimeofday(&epoch, NULL);
    gettimeofday(&start, NULL);
    // start = now;
    seconds_offset = 0;
    hrCount = 0;
    minCount = 0;
  }
  if (TIMEOUT)
  {
    lcd.noBacklight();
  }
  else if(TIMEOUT_WAKEUP)
  {
    lcd.backlight();
    TIMEOUT_WAKEUP = false;
  }

  if(MENU)
  {
    menuScreen();
  }
  else if(POWER)
  {
    sleepHandler();
    wakeupHandler();
  }
  else if(HOME)
  {
    Serial.println("seconds: " + String(seconds));
    Serial.printf("hrCount: %d, minCount: %d\n", hrCount, minCount);
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
        lcd.print("Waiting     ");
        lcd.setCursor(0, 1);
        lcd.print("           ");
      }
    }
  }
  else if(SET_TIME)
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
  else if(DISPLAY_DATA)
  {
    displaySavedData();
  }
}