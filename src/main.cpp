#include <Arduino.h>

// WiFi/Internet
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "RootCACert.h"

// Display 
#include <Wire.h>
#include <Adafruit_SSD1327.h>

// Date/Time
#include <chrono>
#include "date.h"
#include "time.h"
#include <ezTime.h>

// Fonts For Graphics Display
#include <Fonts/FreeSans9pt7b.h>
#include "FuturaBold0345pt7b.h"
#include "FuturaBold037pt7b.h"
#include "FuturaMedium0117pt7b.h"

// Namespaces
using namespace date;
using namespace std::chrono;

// WiFi Config
const char WIFI_SSID[] = "momo-net";
const char WIFI_PASS[] = "freshpots";

// Utility Defines
#define PASS_BOUNDS_BY_REF(B) &(B.x), &(B.y), &(B.width), &(B.height)

// Typedefs
typedef struct {
  int minutes;
  int seconds;
} wait_time;

typedef struct {
  int16_t x;
  int16_t y;
  uint16_t width;
  uint16_t height;
} draw_bounds;

// Globals
WiFiMulti multi;
Timezone timezone;
Adafruit_SSD1327 display(128, 128, &Wire, -1, 1000000);
GFXcanvas1 canvas(128, 128);

// +------------------------------------------------------------+
// + Street Cleaning Utilities                                  +
// +------------------------------------------------------------+

// given a day and a date which represents a cleaning day, this function
// fills "buff" with a stylized message like:
//    North: THIS Fri     +1
//    South: Next Mon     +9
// messages are always 21 characters long
// currently, buffsize is unused
void makeSweepingMessage(char *buff, size_t buffsize, sys_days today, sys_days next_cleaning, int *delta) {
  // figure out what side we're on first
  char side_str[10];
  if(weekday(next_cleaning) == Monday) {
    strcpy(side_str, "South");
  } else if(weekday(next_cleaning) == Friday) {
    strcpy(side_str, "North");
  } else {
    // errrrrr
    return;
  }

  char when_str[20];
  char day_str[20];
  char days_str[20];

  if(today == next_cleaning) {
    strcpy(when_str, "TODAY!");
    strcpy(day_str, "");
    strcpy(days_str, "");
  } else {
    unsigned day_delta = (next_cleaning - today).count();
    sprintf(days_str, "+%i", (unsigned)day_delta);

    if(weekday(next_cleaning) == Monday) {
        strcpy(day_str, "Mon");
    }
    if(weekday(next_cleaning) == Friday) {
        strcpy(day_str, "Fri");
    }

    // pass it out!
    *delta = day_delta;

    if (day_delta < 7) {
      strcpy(when_str, "THIS");
    } else if(day_delta < 14) {
      strcpy(when_str, "Next");
    } else {
      strcpy(when_str, "Later");
    }

  }

  char front_str[17];
  sprintf(front_str, "%s: %s %s", side_str, when_str, day_str);
  sprintf(buff, "%-17s %3s", front_str, days_str);
  
  return;
}

bool isNthDay(sys_days date, int n) {
    year_month_day ymd = year_month_day(date);
    unsigned d = (unsigned)ymd.day();
    return ((n * 7) - d) >= 0 && ((n * 7) - d < 7);
}

// hardcoded to return the next day after start_date
// that is either a 1st or 3rd Friday OR Monday
// there's probably a more efficient way of doing this
// but this is effective and makes sense
sys_days getNextSweepingDay(sys_days start_date) {
    sys_days d = start_date;
    while (1) {
        if (weekday(d) == Monday) {
            if (isNthDay(d, 1)) {
                break;
            }
            if (isNthDay(d, 3)) {
                break;
            }
        }
        if (weekday(d) == Friday) {
            if (isNthDay(d, 1)) {
                break;
            }
            if (isNthDay(d, 3)) {
                break;
            }   
        }
        d += days{1};
    }
    return d;    
}

// +------------------------------------------------------------+
// + Debug Helpers                                              +
// +------------------------------------------------------------+

void debugPrint(const char *str) {
  Serial.print(str);
  display.print(str);
  display.display();
}

void debugPrintln(const char *str) {
  Serial.println(str);
  display.println(str);
  display.display();
}


// +------------------------------------------------------------+
// + Setup Encapsulation                                        +
// +------------------------------------------------------------+

void setupClock() {
  debugPrintln("Wait: Time Sync ");
  configTime(0, 0, "pool.ntp.org");

  ezt::waitForSync();
  timezone.setLocation("America/Los_Angeles");
  Serial.println("Current time : " + timezone.dateTime(RFC1123));
}

void setupWifi() {
  WiFi.mode(WIFI_STA);
  multi.addAP(WIFI_SSID, WIFI_PASS);

  // wait for WiFi connection
  debugPrintln("Wait: WiFi ");
  while (multi.run() != WL_CONNECTED) {
    delay(500);
  }

  setupClock();
}

// +------------------------------------------------------------+
// + Wait Time Helpers                                          +
// +------------------------------------------------------------+

// get next 3 times as minute/second pairs by querying util.egan.me/muni
int getWaitTimes(wait_time *times) {
  
  // make sure the times are empty
  for(int i = 0; i < 3; i++) {
    times[i].minutes = -1;
    times[i].seconds = -1;
  }

  WiFiClientSecure *client = new WiFiClientSecure;
  if (client) {
    client -> setCACert(rootCACertificate);
    {
      Serial.println(F("Requesting latest wait times from internet"));
      HTTPClient https;
      if (https.begin(*client, "https://www.util.egan.me/muni")) {  // HTTPS
        int httpCode = https.GET();
        if (httpCode > 0) {
          Serial.printf("HTTPS Response Code: %d\n", httpCode);

          if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            String payload = https.getString();
            Serial.print("Parsing string: ");
            Serial.println(payload);
            
            // this is so bad
            if(payload[0] == '!') {
              // there was an error
            } else {
              sscanf(payload.c_str(), "%i:%i,%i:%i,%i:%i",
                  &times[0].minutes,
                  &times[0].seconds,
                  &times[1].minutes,
                  &times[1].seconds,
                  &times[2].minutes,
                  &times[2].seconds
              );
              
              for(int i = 0; i < 3; i++) {
                Serial.printf("Got time: %i, %i\r\n", times[i].minutes, times[i].seconds);
              }
            }

          }
        } else {
          Serial.printf("HTTPS GET Failed, error: %s\n", https.errorToString(httpCode).c_str());
          return -1;
        }
        https.end();
      } else {
        Serial.printf("HTTPS Unable to connect\n");
        return -1;
      }
    }

    delete client;
  } else {
    Serial.println("Unable to create client");
    return -1;
  }

  return 1;
}

// decrement a set of 3 wait times (minute/second pairs) by a number of seconds
// modifies values in-place
void decrementWaitTimes(wait_time inTimes[3], wait_time outTimes[3], unsigned int secsSinceCheck) {

  for(int i = 0; i < 3; i++) {
    int total_seconds = (inTimes[i].minutes * 60) + inTimes[i].seconds;
    total_seconds -= secsSinceCheck;

    outTimes[i].minutes = (total_seconds/60);
    outTimes[i].seconds = total_seconds - (outTimes[i].minutes * 60);
  }
}

void print_wait_times(wait_time times[3]) {
  for(int i = 0; i < 3; i++) {
    Serial.printf("%02i:%02i\r\n", times[i].minutes, times[i].seconds);
  }
}

// +------------------------------------------------------------+
// + Drawing Methods                                            +
// +------------------------------------------------------------+

#define SMALL_TEXT_BORDER           2
#define BIG_TEXT_XMARGIN            3
#define BIG_TEXT_YMARGIN            9
#define BIG_TEXT_SMUDGE_FACTOR     -6
#define MINUTES_TEXT_XMARGIN        4
#define LATER_TEXT_YMARGIN          4
#define TIME_BAR_WIDTH              4
#define TIME_BAR_BORDER             1
#define STREET_SWEEPING_FLASH_RATE  1200 // in millis


// occupies a space at the bottom of the screen
bool drawSweepingInfo(draw_bounds *bounds) {

  // get the current time (using ezTime), convert to sys_days
  // making sure to translate between ezTime epoch years (since 1970)
  // to absolute years for date.h
  tmElements_t todayElements;
  ezt::breakTime(timezone.now(), todayElements);
  sys_days today = sys_days(year_month_day(
    year{todayElements.Year + 1970},
    month{todayElements.Month},
    day{todayElements.Day}
  ));

  int daysUntilSweeping;
  char buff[200];

  //gives us the default monospace font
  // need to do this before the call to getTextBounds
  canvas.setFont(NULL); 

  // get the next cleaning day and the message string, figure out how big it needs to be
  sys_days nextCleaningDay = getNextSweepingDay(today);
  makeSweepingMessage(buff, 200, today, nextCleaningDay, &daysUntilSweeping);
  canvas.getTextBounds(buff, 0, 0, &(bounds->x), &(bounds->y), &(bounds->width), &(bounds->height));

  // expand the bounds of the text and locate it corectly, this represents the bounds
  // of the sweeping info box
  bounds->x = 0;
  bounds->y = (int16_t)(128 - bounds->height - (2 * SMALL_TEXT_BORDER));
  bounds->width = 128;
  bounds->height = (uint16_t)(bounds->height + 2 * SMALL_TEXT_BORDER);
  
  
  // draw the bar at the bottom of the screen
  // canvas.fillRect(bounds->x, bounds->y, bounds->width, bounds->height, SSD1327_WHITE);
  // canvas.setTextColor(SSD1327_BLACK);
  canvas.drawFastHLine(bounds->x, bounds->y, bounds->width, SSD1327_WHITE);
  canvas.setTextColor(SSD1327_WHITE);
  canvas.setCursor(bounds->x + SMALL_TEXT_BORDER, bounds->y + SMALL_TEXT_BORDER);
  canvas.print(buff);

  // maybe a hacky place of doing this, but this signfies to the main
  // loop that we should notify the user somehow (eg. stripe the screen)
  return daysUntilSweeping <= 2;
}

void drawClock(draw_bounds *bounds) {

  char buff[200];

  // we want: "Sun Mar 12th      12:48pm"
  String dateString = timezone.dateTime("D M jS"); // longest is (3)(1)(3)(1)(4) == 12
  String timeString = timezone.dateTime("g:ia");  //longest is (2)(1)(2)(2) == 7
  sprintf(buff, "%-12s%9s", dateString.c_str(), timeString.c_str());

  //gives us the default monospace font
  // need to do this before the call to getTextBounds
  canvas.setFont(NULL);
  canvas.getTextBounds(buff, 0, 0, &(bounds->x), &(bounds->y), &(bounds->width), &(bounds->height));

  bounds->x = 0;
  bounds->y = 0;
  bounds->width = 128;
  bounds->height = (uint16_t)(bounds->height + (3 * SMALL_TEXT_BORDER));

  // draws a filled rect (just different)
  // canvas.fillRect(bounds->x, bounds->y, bounds->width, bounds->height, SSD1327_WHITE);
  // canvas.setTextColor(SSD1327_BLACK);

  canvas.drawFastHLine(bounds->x, bounds->y + bounds->height - 1, bounds->width, SSD1327_WHITE);
  canvas.setTextColor(SSD1327_WHITE);
  canvas.setCursor(bounds->x + SMALL_TEXT_BORDER, bounds->y + MINUTES_TEXT_XMARGIN);
  canvas.print(buff);

  return;
}

void drawWaitTimes(wait_time times[3], int secs, draw_bounds *bounds) {

  draw_bounds dummyBigTextBounds, bigTextBounds, laterTextBounds;

  char buff[200];

  //common settings for all text drawn here
  canvas.setTextColor(SSD1327_WHITE);
  canvas.setTextSize(1);
  canvas.setTextWrap(false);

  // draw the main wait time
  sprintf(buff, "%i", times[0].minutes);
  canvas.setFont(&Futura_Bold_0345pt7b);
  canvas.getTextBounds(buff, 0, 0, PASS_BOUNDS_BY_REF(bigTextBounds));
  
  // dummy text bounds so height stays constant for all displayed large characters
  canvas.getTextBounds("9", 0, 0, PASS_BOUNDS_BY_REF(dummyBigTextBounds)); 

  if(times[0].minutes < 10) {
    canvas.setCursor(BIG_TEXT_XMARGIN, bounds->y + dummyBigTextBounds.height + BIG_TEXT_YMARGIN);
  } else {
    canvas.setCursor(BIG_TEXT_XMARGIN + BIG_TEXT_SMUDGE_FACTOR, 
      bounds->y + dummyBigTextBounds.height + BIG_TEXT_YMARGIN);
  }
  
  canvas.print(buff);

  // draw the minutes of the wait time, but only if we have < 10 minutes left
  canvas.setFont(&Futura_Bold_037pt7b);

  // (otherwise there's no space)
  if(times[0].minutes < 10) {
    sprintf(buff, " %i", times[0].seconds);
    int16_t extraMargin = 0;
    if(times[0].minutes == 1) {
      extraMargin = 4;
    }

    canvas.setCursor(
      BIG_TEXT_XMARGIN + bigTextBounds.width + MINUTES_TEXT_XMARGIN + extraMargin, 
      bounds->y + BIG_TEXT_YMARGIN + dummyBigTextBounds.height);

    canvas.print(buff);
  }  


  int nextMins = round(times[1].minutes + times[1].seconds/60.0);
  int laterMins = round(times[2].minutes + times[2].seconds/60.0);
  
  sprintf(buff, "And: %i, %i", nextMins, laterMins);
  canvas.getTextBounds(buff, 0, 0, PASS_BOUNDS_BY_REF(laterTextBounds));
  canvas.setCursor(
    BIG_TEXT_XMARGIN, 
    bounds->y + BIG_TEXT_YMARGIN + dummyBigTextBounds.height + LATER_TEXT_YMARGIN + laterTextBounds.height);
  canvas.print(buff);

  // print a bar showing when we'll reload times
  int internalBarHeight = bounds->height - (TIME_BAR_BORDER * 6);
  int bar_height = (60 - secs) * (internalBarHeight/ 60.0);

  canvas.fillRect(
    bounds->x + bounds->width - (TIME_BAR_WIDTH) - (TIME_BAR_BORDER * 5),
    bounds->y + (TIME_BAR_BORDER * 1),
    TIME_BAR_WIDTH + (TIME_BAR_BORDER * 4),
    bounds->height -(TIME_BAR_BORDER *2),
    SSD1327_WHITE
  );

  canvas.fillRect(
    bounds->x + bounds->width - (TIME_BAR_WIDTH) - (TIME_BAR_BORDER * 4),
    bounds->y + (TIME_BAR_BORDER * 2),
    TIME_BAR_WIDTH + (TIME_BAR_BORDER * 2),
    bounds->height -(TIME_BAR_BORDER * 4),
    SSD1327_BLACK
  );

  canvas.fillRect(
    bounds->x + bounds->width - (TIME_BAR_WIDTH) - (TIME_BAR_BORDER * 3),
    bounds->y + (TIME_BAR_BORDER * 3) + (internalBarHeight - bar_height),
    TIME_BAR_WIDTH,
    bar_height,
    SSD1327_WHITE);

  return;

}

void invertCanvasRect(GFXcanvas1 *c, draw_bounds rect) {
  uint16_t xlim = rect.x + rect.width;
  uint16_t ylim = rect.y + rect.height;

  for(int16_t x = rect.x; x < xlim; x++) {
    for(uint16_t y = rect.y; y < ylim; y++) {
      if(x < 128 && y < 128) {
        if(c->getPixel(x, y)) {
          c->drawPixel(x, y, SSD1327_BLACK);
        } else {
          c->drawPixel(x, y, SSD1327_WHITE);
        }
      }
    }
  }
}

// +------------------------------------------------------------+
// + Arduino Routines                                           +
// +------------------------------------------------------------+

void setup() {

  // setup serial port
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  Serial.println();

  // setup display
  display.begin(0x3D);
  display.setRotation(1);
  display.clearDisplay();
  display.display();

  setupWifi();

}

void loop() {

  static unsigned long updateTime = 0;
  static unsigned long lastFlashTime = 0;
  static wait_time times[3];
  wait_time decrementedTimes[3];

  // determine if we need to request new wait times or 
  // if we can decrement the ones we already have by the number
  // of seconds since we last checked
  int secondsSinceLastRequested = (int)((millis() - updateTime) / 1000.0);
  if(secondsSinceLastRequested < 60 && updateTime != 0) {
    decrementWaitTimes(times, decrementedTimes, secondsSinceLastRequested);
  } else {
    getWaitTimes(times);
    decrementWaitTimes(times, decrementedTimes, 0);
    updateTime = millis();
  }

  // clear the canvas and get it ready to show some stuff
  canvas.fillScreen(0x0000);

  // draw the clock and street sweeping messages
  // the clock and sweeping bars determine their own bounds
  draw_bounds clockBounds, sweepingBounds;
  bool shouldScream = drawSweepingInfo(&sweepingBounds);
  drawClock(&clockBounds);

  // we draw the wait times and the seconds remaining bar in the 
  // central rectangular area bounded at the top and bottom by
  // the top and bottom bars (datetime/sweeping info)
  draw_bounds waitTimeBounds = {
    .x = 0,
    .y = (int16_t)(clockBounds.y + clockBounds.height),
    .width = 128,
    .height = (uint16_t)(sweepingBounds.y - (clockBounds.y + clockBounds.height))
  };

  drawWaitTimes(decrementedTimes, secondsSinceLastRequested, &waitTimeBounds);

  // if there's street cleaning within the next few days
  // flicker the street cleaning bar so it's noticable
  if(shouldScream) {
  draw_bounds invertBounds = sweepingBounds;
    if((millis() - lastFlashTime) > STREET_SWEEPING_FLASH_RATE) {
      lastFlashTime = millis();
      invertBounds.y += 1;
      invertCanvasRect(&canvas, invertBounds);
    }
  }

  //push the canvas to the display!
  display.clearDisplay();
  display.setRotation(1);
  display.drawBitmap(0, 0, canvas.getBuffer(), 128, 128, 0xFFFF, 0x0000);
  display.display();

  // blit the canvas to Serial if someone asks
  if(Serial.available()) {
    Serial.read();

    for(int y = 0; y < 128; y++) {
      for(int x = 0; x < 128; x += 8) {
        char byteOfPixels = 0x00;
        for(int i = 0; i < 8; i++) {
          byteOfPixels |= ((char)canvas.getPixel(x + i, y)) << (7 - i);
        }
        Serial.printf("%02x",byteOfPixels);
      }
    }
    Serial.println();
  }
  

}