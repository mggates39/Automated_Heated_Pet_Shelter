
/**************************************************************************/
/*!
This is a demo for the Adafruit MCP9808 breakout
----> http://www.adafruit.com/products/1782
Adafruit invests time and resources providing this open source code,
please support Adafruit and open-source hardware by purchasing
products from Adafruit!
*/
/**************************************************************************/

#include <Wire.h>
#include <Adafruit_MCP9808.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "MyDelay.h"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define RELAY_PIN 8
#define DEFAULT_READ_TEMP_TIMER 60000
#define DEFAULT_RECYCLE_DELAY 900000
#define DEFAULT_HEAT_TIME 2700000


// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Create the MCP9808 temperature sensor object
Adafruit_MCP9808 inside_sensor = Adafruit_MCP9808();
Adafruit_MCP9808 outside_sensor = Adafruit_MCP9808();

long read_temp_delay = DEFAULT_READ_TEMP_TIMER;
long recycle_delay = DEFAULT_RECYCLE_DELAY;
long active_delay = DEFAULT_HEAT_TIME;

myDelay occupancy_timer = myDelay(300000);          // Check occupancy every 5 minutes 300000
myDelay sleep_temp_timer = myDelay(2000);           // Leave sensors running for 2 seconds 2000
myDelay read_temp_timer = myDelay(read_temp_delay); // Check Temperatures every 1 minute 60000
myDelay heat_on_timer = myDelay(active_delay);      // Keep heat on for 45 minutes 45*60*1000 = 2700000
myDelay heat_reset_timer = myDelay(recycle_delay);  // Cycle heat off for 15 minutes 15*60*1000 = 900000

float inside_temp, outside_temp;

boolean system_on = false;
boolean occupied = false;
boolean heat_on = false;
boolean cold_out = false;
boolean cold_in = false;

boolean run_state_machine = true;

void setup() {
  Serial.begin(115200);
  while (!Serial); //waits for serial terminal to be open, necessary in newer arduino boards.
  Serial.println("Cat Palace Heater");

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
  delay(2000); // Pause for 2 seconds

  // Clear the buffer
  display.clearDisplay();
  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font

  if (!inside_sensor.begin(0x18)) {
    Serial.println("Couldn't find inside MCP9808!");
    while (1);
  }
  if (!outside_sensor.begin(0x19)) {
    Serial.println("Couldn't find outside MCP9808!");
    while (1);
  }
    
  inside_sensor.setResolution(0); // sets the resolution mode of reading, the modes are defined in the table bellow:
  outside_sensor.setResolution(0); // sets the resolution mode of reading, the modes are defined in the table bellow:

  sleep_temp_timer.setRepeat(1);
  heat_on_timer.setRepeat(1);
  heat_reset_timer.setRepeat(1);
  occupancy_timer.start();
  
  turn_system_off();
  read_load_cell();
  read_temps();
}

void turn_system_off() {
  system_on = false;

  read_temp_delay = 60 * 60 * 1000;
  read_temp_timer.setDelay(read_temp_delay);
  read_temp_timer.start();

  heat_on_timer.stop();
  turn_off_heat();
  heat_reset_timer.stop();
}

void turn_system_on() {
  system_on = true;

  read_temp_delay = 10 * 60 * 1000;
  read_temp_timer.setDelay(read_temp_delay);
  read_temp_timer.start();

  heat_on_timer.stop();
  active_delay = 5 * 60 * 1000;
  heat_on_timer.setDelay(active_delay);
  
  recycle_delay = 55 * 60 * 1000;
  heat_reset_timer.setDelay(recycle_delay);
  turn_off_heat();
  heat_reset_timer.stop();
}

void highlight_status(bool flag, char *tag) {
  if (flag) {
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }
  display.print(tag);
  display.setTextColor(SSD1306_WHITE);
  display.print(" ");
}

void read_load_cell() {
  /*
   * Read cell
   * if cell shows weight
   *  if not occupied
   *    set occupied
   *    reduce temp timer to default
   * else
   *  if occupide
   *    clear occupied
   *    increast temp timer to 10 minutes
   */
  occupied = true;
}

void read_temps() {
  digitalWrite(LED_BUILTIN, HIGH);

  inside_sensor.wake();   // wake up, ready to read!
  outside_sensor.wake();   // wake up, ready to read!
  
  // Read the inside temperature
  inside_temp = inside_sensor.readTempF();
 
  // Read the outside temperature,
  outside_temp = outside_sensor.readTempF();

  sleep_temp_timer.start();

}

void sleep_probes() {
  digitalWrite(LED_BUILTIN, LOW);
  
  inside_sensor.shutdown_wake(1); // shutdown MSP9808 - power consumption ~0.1 mikro Ampere, stops temperature sampling
  outside_sensor.shutdown_wake(1); // shutdown MSP9808 - power consumption ~0.1 mikro Ampere, stops temperature sampling  
}

void turn_on_heat() {
  digitalWrite(RELAY_PIN, HIGH);
  heat_on = true;
  heat_on_timer.start();
}

void turn_off_heat() {
  digitalWrite(RELAY_PIN, LOW);
  heat_on = false;
  heat_reset_timer.setDelay(recycle_delay);
  heat_reset_timer.start();
}

void process_temperature() {
  
  if (inside_temp < 25.0) {
    if (!cold_in) {
      cold_in = true;
      active_delay = 55 * 60 * 1000;
      heat_on_timer.setDelay(active_delay);
      recycle_delay = 5*60*1000;
      heat_reset_timer.setDelay(recycle_delay);
    }
  } else {
    if (cold_in) {
      active_delay = DEFAULT_HEAT_TIME;
      heat_on_timer.setDelay(active_delay);
      recycle_delay = DEFAULT_RECYCLE_DELAY;
      heat_reset_timer.setDelay(recycle_delay);
      cold_in = false;
      cold_out = false;
    }
  }
  
  if (outside_temp < 35.0) {
    if (!cold_out) {
      cold_out = true;
      active_delay = 50 * 60 * 1000;
      heat_on_timer.setDelay(active_delay);
      recycle_delay = 10*60*1000;
      heat_reset_timer.setDelay(recycle_delay);
    }
  } else {
    if (cold_out) {
      active_delay = DEFAULT_HEAT_TIME;
      heat_on_timer.setDelay(active_delay);
      recycle_delay = DEFAULT_RECYCLE_DELAY;
      heat_reset_timer.setDelay(recycle_delay);
      cold_out = false;
    }
  }
  
  if (outside_temp <= 45.0) {
    if (!heat_on) {
      active_delay = DEFAULT_HEAT_TIME;
      heat_on_timer.setDelay(active_delay);
      recycle_delay = DEFAULT_RECYCLE_DELAY;
      heat_reset_timer.setDelay(recycle_delay);

      turn_on_heat();
    }
  } else {
    if (heat_on) {
      turn_off_heat();
    }
    heat_reset_timer.stop();
  }

}

void process_state_machine() {
  if (system_on) {
    if (outside_temp > 60.0) {
      turn_system_off();
    } else {
      if (occupied) {
        process_temperature();
      }
    }
    
  } else {
    if (outside_temp < 50.0) {
      turn_system_on();
    }
  }
}

void loop() {

  if (occupancy_timer.update()) {
    read_load_cell();
  }

  if (sleep_temp_timer.update()) {
    sleep_probes();
  }

  if (heat_on_timer.update()) {
    turn_off_heat();
  }
  
  if (heat_reset_timer.update()) {
    turn_on_heat();
  }

  if (read_temp_timer.update()) {
    read_temps();
    run_state_machine = true;
  }

  if (run_state_machine) {
    process_state_machine();
    run_state_machine = false;
  }
  
  display.clearDisplay();
  display.setCursor(0, 0);     // Start at top-left corner


  // Print out the temperature
  display.print("Inside  Temp: "); 
  display.print(inside_temp, 1); display.println(" F.");
  
  display.print("Outside Temp: "); 
  display.print(outside_temp, 1); display.println(" F.");
  
  display.println("");
  highlight_status(system_on, "Sys");
  highlight_status(occupied, "Occ");
  highlight_status(cold_out, "Out");
  highlight_status(cold_in, "In");
  highlight_status(heat_on, "Heat");
  display.println("");
  display.display();
}