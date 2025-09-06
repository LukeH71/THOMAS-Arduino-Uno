/*
      THOMAS 
      - Temperature & Humidity Observing/Monitoring Assembled Sensor
    Arduino Uno Code

  This is the code for a temperature logger that saves the temperature and humidity over time
  to a microSD card, in a format that is easy to use in graphs through importing to programs
  such as google spreadsheet.

  When you start the program, you will be prompted on the digital display to insert an SD card.
  Once inserted, and a button is pressed to start, the temperature starts to log. Once finished,
  press the button to access the menue, scroll to the "off" button, and click it. You can then
  scan that microSD from a computer, find the correct .dat file, and import it into a program
  such as google sheets or exel.

  Wiring:

  DHT22
    VCC -> 5V
    GND -> GND
    DATA -> D2

  SD Card Module
    VCC -> 5V
    GND -> GND
    CS -> D10
    MOSI -> D11
    MISO -> D12
    SCK -> D13

  RTC (DS3231)
    VCC -> 5V
    GND -> GND
    SDA -> A4
    SCL -> A5

  LCD1602 I²C
    VCC -> 5V
    GND -> GND
    SDA -> A4
    SCL -> A5

  Push Button
    VCC -> 5V
    OUTPUT -> D7

  LED (red)
    GND -> GND
    VCC -> D4
*/


#include "DHT.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <SD.h>
#include "RTClib.h"

// Define pins
const int chipSelect = 10;  // SD card reader chip select wired to D10
const int ledPin = 4; // Red LED wired to D4
const int buttonPin = 7; // Menue / navigator button wired to D7
#define DHTPIN 2 // Temperature logger data pin wired to D2
#define DHTTYPE DHT22 // Define sensor type (DHT22, not DHT11)

// Define modules
LiquidCrystal_I2C lcd(0x27, 16, 2); // LCD
DHT dht(DHTPIN, DHTTYPE); // DHT22 / temperature logger
RTC_DS3231 rtc; // RTC / real time clock

// Enums for code readability
enum Modes {
  OFF = 0,
  MODE_SELECT = 1, // Also refered to as menue
  TEMP_HUMIDITY_VIEW = 2,
  DURATION_BYTES_VIEW = 3
};

enum States {
  ERROR_RTC_LOST_POWER = 1,
  ERROR_DATAFILE_FAILED = 5,
  ERROR_TIME_TXT_NOT_FOUND = 7,
  ERROR_TIME_TXT_FORMAT_INCORRECT = 8,

  DEFAULT = 2,

  READY_FOR_BUTTON_INPUT = 3,
  BUTTON_PRESSED = 4,

  DONE_WORKING_MAY_REMOVE_SD = 6
};

// Mode and state variables. 'mode' is to select between 4 different main modes: off,
// menue/mode select, temp/humidity view, and duration/bytes view. 'state' is used as a
// selection of different states within each mode.
// 
// The 'state' variable is used in the screen idling logic, the off screen, and the menue.
// However, the use of the variable varies drastically in each instance, as to reduce the
// global namespace, and to maintain efficiency. In the idling logic, it is used to manage
// button presses. In the off screen, it is used as different messages to display. In the
// menue, it is used to select which mode to enter.
Modes mode;
States state = DEFAULT;

// Various timers/cooldowns
unsigned long lastPressTime = 0; // Menue cooldown to actually enter a mode
unsigned long idolingTime = 0; // Screen idling
unsigned long logCooldown = 0; // Log cooldown to prevent a log every clock cycle
unsigned long preventDoubleClick = 0; // Used in the menue to prevent electrical false double-clicks

// The reference time, which is used to correctly assign each log with a timestamp
unsigned int refTime;

// Different durations / frequencies
#define IDOLING_DURATION 20000
#define REMAIN_IDOL_DURATION 30000
#define LOG_FREQUENCY 5000
#define MENUE_SELECT_DURATION 1000
#define COUNTER_FAKE_BUTTON_RELEASE_DURATION 50


// The statistics viewable in the latter 2 modes
unsigned long numBytes = 0;
unsigned int timeSeconds = 0;
float humidity = 0; 
float temperature = 0; 

// A counter used to preserve data incase of unplugging, but also displayed occationally
unsigned long numDataPoints = 0;

// The file in which logs are stored
File dataFile;




// Initialization
void setup() {

  // Set up communication between computer and arduino for debugging
  Serial.begin(9600);

  // Initialize sensors

  dht.begin();
  lcd.init();       // Initialize the LCD
  lcd.backlight();  // Turn on the backlight

  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting time!");

    logCooldown = millis(); // In this instance, logCooldown is used to temprorarially display an error message
    state = ERROR_RTC_LOST_POWER;
  }

  // Get the reference time for timestamping
  DateTime reference = rtc.now();
  refTime = reference.unixtime();

  // Setup / set initial values of the LED & button pin
  pinMode(ledPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);

  digitalWrite(ledPin, HIGH);
}



// Set up 'dataFile' with the correct name. This is called in mode == 0 / OFF
void defineDateFile(){


  if(rtc.lostPower() && !SD.exists("time.txt")) { // If the rtc lost power, and there is no replacement, no name can be generated
    state = ERROR_TIME_TXT_NOT_FOUND;
    return;

  } else if(rtc.lostPower()) { // If the rtc lost power, but there is a possible replacement, try to find the name

    timeFile = SD.open("time.txt", FILE_READ);

    // Save the contents of 'timeFile' into 'savedName'
    char savedName[64];
    int len = timeFile.readBytesUntil('\n', savedName, sizeof(savedName)-1);
    savedName[len] = '\0'; // Null terminate
    timeFile.close();

    // Verify the name, and set the RTC

    // The final date variable, formatted in: MM DD YY HH mm SS XX
    char date[7][3] = {{'0','0','\0'},{'0','0','\0'},{'0','0','\0'},{'0','0','\0'},{'0','0','\0'},{'0','0','\0'},{'0','0','\0'}};

    int format = 0;
    int concurrentUnderlines = 0;
    int concurrentNumbers = 0;
    
    // Values for 'format' and what they correspond to:
    // Pre-format forms (not actually valid):
    //
    // 0 - MM / 7
    // 1 - MM_DD / 8
    //
    // Supported Values
    // (x = version X / X file named under the same name)
    //
    // 2 - MM_DD_YY
    // 3 - MM_DD_YY__XX 
    // 4 - MM_DD_YY_HH
    // 5 - MM_DD_YY_HH__XX
    // 6 - MM_DD_YY_HH_mm
    // 7 - MM_DD_YY_HH_mm_SS
    //
    


    for (int i = 0; i<len; ++i){
      if(isAlpha(savedName[i]) && savedName[i] != '_'){ // Call an error if a character is neither a letter, nor an '_'
        state = ERROR_TIME_TXT_FORMAT_INCORRECT;
        return;

      } else { // All valid letters:

        if (savedName[i] == '_') { // Specifically underscores:
          concurrentUnderlines += 1;

          if (i==0){ // Time.txt cannot start with '_'
            state = ERROR_TIME_TXT_FORMAT_INCORRECT;
            return;
          }

          if(concurrentUnderlines == 2) { // A double underscore can only uccor on the 2 formats where an X is supported
            if (format == 2){
              format = 3;
            } else if (format == 4) {
              format = 5;
            } else {
              state = ERROR_TIME_TXT_FORMAT_INCORRECT;
              return;
            }
          } else if (concurrentUnderlines > 2){ // A triple+ underscore is never supported
            state = ERROR_TIME_TXT_FORMAT_INCORRECT;
            return;
          }

        } else { // All numerical characters

          concurrentNumbers += 1;

          if (concurrentNumbers > 2) { // Numbers can only occur in packets of 2: MM_DD_YY etc.
            state = ERROR_TIME_TXT_FORMAT_INCORRECT;
            return;
          }

          if(concurrentUnderlines == 1){ // Direct conversion between formats seperated between underscores
            switch (format){
              case 0: format = 1; break;
              case 1: format = 2; break;
              case 2: format = 4; break;
              case 4: format = 6; break;
              case 6: format = 7; break;
            }
            concurrentUnderlines = 0;
          }

          // Adjust the date list (for initializing the RTC)
          // x = which feild is being edited. Since each format increase increases the feild count, then they are
          // directly preportional. However, feilds that use the __X suffix jump right to the 6th feild.
          // Because of this, they are subracted if passed, and if format is equal to them, then X is set to 6.
          int x = format - (format > 3) - (format > 5); 
          if (format == 3 || format == 5) { x = 6; }
          if (date[x][1]!='0'){ date[x][0]=date[x][1]; } // Move the value over to not truncate numbers
          date[x][1] = savedName[i];
          
        }
      }
    }

    // MM DD YY HH mm SS XX -> YY MM DD HH mm SS
    rtc.adjust(DateTime((atoi(date[2])+2000), atoi(date[0]), atoi(date[1]), atoi(date[3]), atoi(date[4]), atoi(date[5])));

    // If the file name exists, and there is not a number ontop of it already, append a __X
    if (SD.exists(savedName)&&format!=2&&format!=4){
      int i = 0;

      // Loop until a number that does not exist is found
      while (true){
        ++i;
        char fileName[64];

        snprintf(fileName, sizeof(fileName), "%s__%i.dat", savedName, i);

        if(!SD.exists(fileName)){
          strcpy(savedName, fileName);
          break;
        }

      }

    } else if (SD.exists(savedName)) { // If the file name exists, but there is already an __X suffix, throw an error, because the user needs more/less detail
      state = ERROR_TIME_TXT_FORMAT_INCORRECT;
      return;

    } else { // If the file does not exist, create the file name
      snprintf(savedName, sizeof(savedName), "%s.dat", savedName);
    }

    // Open the file name :)
    dataFile = SD.open(savedName, FILE_WRITE);
    SD.remove("time.txt");

  } else { // If the RTC remained to have power, create the file based off of that in format 6 (MM_DD_YY_HH_mm)
    char fileName[64];

    snprintf(fileName, sizeof(fileName), "%02d_%02d_%02d_%02d_%02d.dat", rtc.now().month(), rtc.now().day(), (rtc.now().year()-2000), rtc.now().hour(), rtc.now().minute());

    // This level of accuracy should not already have a file attatched to it, so it can confidentally be assigned a value
    dataFile = SD.open(fileName, FILE_WRITE);
  }
}









// Main loop
void loop() {

  // Logic for logging the temp/humidity as long as the mode != 0 / off
  // + logic for idling the screen to prevent overheating
  if (mode != 0) {

    // Logging logic
    if (millis() - logCooldown > LOG_FREQUENCY) {
      ++numDataPoints;

      log();
      logCooldown = millis();

      if (numDataPoints % 5 == 0){ // Incase of accidental poweroff
        dataFile.flush();
      }
    }

    // Screen idling logic (as long as mode != 1 / mode select)
    if (mode != 1) { 
      screenIdle();
    }
  }

  // Handle each mode individually in a switch statement
  switch (mode) {
    case 0: // The logic of inputing or taking out SD 
      offMode();
      break;
    case 1: // Menue to change what to display, or to turn off
      menue();
      break;
    case 2: // Temp / humidity
      if (millis() - idolingTime > IDOLING_DURATION) { // Don't print if screen is idle
        lcd.setCursor(0,0);
        lcd.print("Temp: ");
        lcd.print(temperature);
        lcd.print(" degC ");
        lcd.setCursor(0,1);
        lcd.print("Humid: ");
        lcd.print(humidity);
        lcd.print(" %RH ");
      }
      break;
    case 3: // Duration / file size
      if (millis() - idolingTime > IDOLING_DURATION) { 
        lcd.setCursor(0,0);
        lcd.print(timeSeconds);
        lcd.print(" Sec elps        ");
        lcd.setCursor(0,1);
        if(numDataPoints%2==0) { // Toggle between the number of data points and the number of bytes
          lcd.print("~");
          lcd.print(numBytes);
          lcd.print(" Bytes            ");
        } else {
          lcd.print(numDataPoints);
          lcd.print(" Data pts            ");
        }
      }
      break;
  }
}




// Log the time to the .dat file, while keeping track of the number of bits
void log() {

  timeSeconds = rtc.now().unixtime()-refTime;

  humidity = dht.readHumidity();
  temperature = dht.readTemperature(); // Celsius

  // Check if any reads have failed
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT22 sensor!");
    return;
  }
 
  numBytes += dataFile.print(timeSeconds);
  numBytes += dataFile.print(","); 
  numBytes += dataFile.print(temperature);
  numBytes += dataFile.print(",");
  numBytes += dataFile.print(humidity);
  numBytes += dataFile.println();
}



// Idle the screen to preserve battery, requiring a button press to wake up
void screenIdle(){

  // Turn off backlight after 20 inactive seconds, with a clause ensuring that a call to
  // turn off the backlight won't be called every loop cycle after 30 inactive seconds
  if (millis() - idolingTime > IDOLING_DURATION && millis() - idolingTime < REMAIN_IDOL_DURATION) {
    lcd.noBacklight();
  }

  // Handle waking up, using 'state' to ensure that one button press, and multiple
  // loop cycles of holding down the button won't cause the mode to switch immidately
  // to the menue.
  //
  // state == 2       Default value before waking the screen
  // state == 3       Value after pressing the button to wake the screen
  // state == 4       After releasing the button to wake the screen, meaning that
  // the user pressed the button, and released it.

  int buttonState = digitalRead(buttonPin);
  if (buttonState == LOW && (state == 2 || state == 4)) {

    if (millis() - idolingTime < IDOLING_DURATION && state == 4) {
      mode = MODE_SELECT;
      lastPressTime = millis();

    } else {
      idolingTime = millis();
      lcd.backlight();
    }

    state = 3;
      
  } else if (buttonState == HIGH && state == 3) {
    state = 4;
  }
}






// Mode 0 - off mode 
// Inserting SD, formatting SD, and removing SD
void offMode(){

  int buttonState = digitalRead(buttonPin);

  // Handle different cases 
  switch(state) {
    case ERROR_RTC_LOST_POWER:
      lcd.setCursor(0, 0);
      lcd.print("RTC lost power,");
      lcd.setCursor(0, 1);
      lcd.print("input time 2 SD");
      if (millis() - logCooldown < 7000){
        state = 3;
      }
      break;
    case ERROR_DATAFILE_FAILED:
      lcd.setCursor(0, 0);
      lcd.print("Error starting,    ");
      lcd.setCursor(0, 1);
      lcd.print("please retry :)    ");
      state = 3;
      break;
    case ERROR_TIME_TXT_NOT_FOUND:
      lcd.setCursor(0, 0);
      lcd.print("Error: time.txt");
      lcd.setCursor(0, 1);
      lcd.print("file not found.");
      state = 3;
      break;
    case ERROR_TIME_TXT_FORMAT_INCORRECT:
      lcd.setCursor(0, 0);
      lcd.print("Error: time.txt");
      lcd.setCursor(0, 1);
      lcd.print("formatted wrong");
      state = 3;
      break;
    case DEFAULT:
      lcd.setCursor(0, 0);
      lcd.print("Please insert  ");
      lcd.setCursor(0, 1);
      lcd.print("SD & press ->  ");
      state = 3;
      break;
    case DONE_WORKING_MAY_REMOVE_SD:
      lcd.setCursor(0, 0);
      lcd.print("You may remove ");
      lcd.setCursor(0, 1);
      lcd.print("SD or restart  ");
      state = 3;
      break;
  }

  // Handle RTC lost power error case as well, because the warning is just a temporary popup, and can be ignored
  if (buttonState == LOW && (state==READY_FOR_BUTTON_INPUT || state==ERROR_RTC_LOST_POWER)) {
    state = BUTTON_PRESSED;

  } else if (buttonState == HIGH && state==BUTTON_PRESSED) {

    if (!SD.begin(chipSelect)) {
      state = ERROR_DATAFILE_FAILED;
      return;
    }

    // Formatting correctly requires complexity, and such is packaged into a function.
    defineDateFile(); // Define the data file through the RTC, and time.txt if applicable
    // skip the following code if an error is thrown in the above function to prevent from crashing / undifined behavior
    if (state == ERROR_TIME_TXT_FORMAT_INCORRECT || state == ERROR_TIME_TXT_NOT_FOUND ) { return; }
    
    if (!dataFile) {
      state = ERROR_DATAFILE_FAILED;

    } else {

      numBytes += dataFile.println("Time (Seconds),Temperature (Celcius), Humidity (%RH)");

      state = DEFAULT;

      idolingTime = millis();
      mode = TEMP_HUMIDITY_VIEW;
      digitalWrite(ledPin, LOW); // LED is only active when not recording
    }
  }
}






// Mode 1 - menue / mode switcher mode 
// Changing between which data to view, and turning off
//
// Meaning of each 'state' variable can be found by:
// The number int(state)/2 corresponds to if it is reffering to:
//   1) Temp / humidity
//   2) Duration / datafile size
//   3) off
// The number state%2 correspons to:
//   0) The button is not being pressed down
//   1) The button is being pressed down

void menue(){

  // Go to the mode if button has not been pressed in one second
  if (millis() - lastPressTime > MENUE_SELECT_DURATION && state%2==0) {

    switch(state){
      case 2:
        mode = TEMP_HUMIDITY_VIEW;
        state = DEFAULT;

        idolingTime = millis();

        break;
      case 4:
        mode = DURITION_BYTES_VIEW;
        state = DEFAULT;

        idolingTime = millis();

        break;
      case 6:
        mode = OFF;
        state = DONE_WORKING_MAY_REMOVE_SD;

        dataFile.close();
        digitalWrite(ledPin, HIGH);

        break;
    }
  }

  
  // Handle button clicks and changing between selections. 'preventDoubleClick' is used
  // so that short, fast, electronic signals do not randomly cause a double click, making
  // the selection skip one element
  int buttonState = digitalRead(buttonPin);
  if (buttonState == LOW && state%2==0) {

    state += 1;

    // Loop back to the first element if already seen all previous elements
    if(state>5){
      state -= 6;
    }
    
    // Type an '→' character next to the current selection
    switch (state) {
      case 1:
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print(" ");
        lcd.write(byte(126));
        lcd.print("1     2     3");
        
        break;
      
      case 3:
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("  1    ");
        lcd.write(byte(126));
        lcd.print("2     3");
        
        break;

      case 5:
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("  1     2    ");
        lcd.write(byte(126));
        lcd.print("3");
        
        break;
    }

    // The choices between menues
    lcd.setCursor(0,1);
    lcd.print("temp  info  off");

  } else if(buttonState == HIGH && state%2==1) { // Handle fake double clicks
    state += 1;
    preventDoubleClick = millis() + COUNTER_FAKE_BUTTON_RELEASE_DURATION;
  } else if (buttonState == HIGH && state%2==0 && preventDoubleClick - millis() > 0){
    lastPressTime = millis();
    preventDoubleClick = 0;
  }
}




