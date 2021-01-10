/**************************************************************************/
/*!
This is based on a demo for the Adafruit MCP9808 breakout
----> http://www.adafruit.com/products/1782
Adafruit invests time and resources providing this open source code,
please support Adafruit and open-source hardware by purchasing
products from Adafruit!

This controls a heat source for an outside pet house.
It uses a load sensor to determine if the house is occupied.
It also has two temperature sensors.  One inside the house and the
other outside.  

These drive the heat source to be on or off to help maintain a comfortable
temperature inside the house.

Uses the MyDelay Arduino library at https://github.com/mggates39/MyDelay
*/
/**************************************************************************/

#include <Wire.h>
#include <Adafruit_MCP9808.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HX711.h>
#include <MyDelay.h>


#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

#define LOADCELL_DOUT_PIN 2
#define LOADCELL_SCK_PIN 3
#define RELAY_PIN 8

// Default read time 1 * 60 * 1000 is 1 minute or 60000 milliseconds
#define DEFAULT_READ_TEMP_TIMER 60000
// default heater off time 15 * 60 * 1000 is 15 minutes or 900000 milliseconds
#define DEFAULT_RECYCLE_DELAY 900000
// default heater on time 45 * 60 * 1000 is 45 minutes or 2700000 milliseconds
#define DEFAULT_HEAT_TIME 2700000

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Create the MCP9808 temperature sensor object
Adafruit_MCP9808 inside_sensor = Adafruit_MCP9808();
Adafruit_MCP9808 outside_sensor = Adafruit_MCP9808();


HX711 scale = HX711();

// Pre-define call back functions for timers
void read_load_cell();
void read_temps();
void sleep_probes();
void turn_off_heat();
void turn_on_heat();
void done_startup_mode();

long read_temp_delay = DEFAULT_READ_TEMP_TIMER;
long recycle_delay = DEFAULT_RECYCLE_DELAY;
long active_delay = DEFAULT_HEAT_TIME;

MyDelay startup_timer = MyDelay(300000, done_startup_mode,1);       // We are in startup for 5 minutes 5*60*1000 = 300000ms, only fire once
MyDelay occupancy_timer = MyDelay(300000, read_load_cell);          // Check occupancy every 5 minutes 5*60*1000 = 300000ms, repeat forever
MyDelay sleep_temp_timer = MyDelay(2000, sleep_probes, 1);          // Leave sensors running for 2 seconds 2*1000 = 2000ms, only fire once
MyDelay read_temp_timer = MyDelay(read_temp_delay, read_temps);     // Check Temperatures every 1 minute 60*1000 = 60000ms, repeat forever
MyDelay heat_on_timer = MyDelay(active_delay, turn_off_heat, 1);    // Turn heat off after 45 minutes 45*60*1000 = 2700000ms, only fire once
MyDelay heat_reset_timer = MyDelay(recycle_delay, turn_on_heat, 1); // Turn heat oon after 15 minutes 15*60*1000 = 900000ms, only fire once

float inside_temp, outside_temp;

boolean startup_mode = false;
boolean system_on = false;
boolean occupied = false;
boolean heat_on = false;
boolean cold_out = false;
boolean cold_in = false;
boolean scale_found = false;
boolean indoor_missing = false;
boolean outdoor_missing = false;

boolean run_state_machine = true;

void setup() {
  Serial.begin(115200);
  while (!Serial); //waits for serial terminal to be open, necessary in newer arduino boards.
  Serial.println(F("Cat Palace Heater"));

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
  delay(1000); // Pause for 1 second

  // Clear the buffer and display banner page
  display.clearDisplay();
  display.setTextSize(2);      // Medium 2:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
  display.println(F("Cat Palace\n  Heater"));
  display.display();
  delay(1000); // Pause for 1 second

  // Set up for normal display
  display.clearDisplay();
  display.setCursor(0, 0);     // Start at top-left corner
  display.setTextSize(1);      // Normal 1:1 pixel scale

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  if (scale.is_ready()) {
    scale_found = true;
    scale.set_scale(2280.f);
    scale.tare();
  } else {
    Serial.println(F("HX711 did not respond"));
    occupied = true;
  }

  if (!inside_sensor.begin(0x18)) {
    Serial.println("Couldn't find inside MCP9808!");
    indoor_missing = true;
  }
  
  if (!outside_sensor.begin(0x19)) {
    Serial.println(F("Couldn't find outside MCP9808!"));
    outdoor_missing = true;
  }

  Serial.println(F("Done Checking"));

  while(indoor_missing || outdoor_missing);
    
  inside_sensor.setResolution(0); // sets the resolution mode of reading.
  outside_sensor.setResolution(0); // sets the resolution mode of reading.

  occupancy_timer.start();
  
  turn_system_off();
  
  read_load_cell();
  
  read_temps();

  enter_startup_mode();
}

void set_read_delay( long milliseconds) {
  read_temp_delay = milliseconds;
  read_temp_timer.setDelay(read_temp_delay);
  read_temp_timer.start();
}

void turn_system_off() {
  system_on = false;

  set_read_delay( 60 * 60 * 1000L); // 60 minutes

  heat_on_timer.stop();
  turn_off_heat();
  heat_reset_timer.stop();
}

void turn_system_on() {
  system_on = true;
  
  if (startup_mode) {
    startup_timer.stop();
    done_startup_mode();
  }

  set_read_delay(DEFAULT_READ_TEMP_TIMER);

  heat_on_timer.stop();
  set_unoccupied_heat_cycle();
  turn_on_heat();
}

void enter_startup_mode() {
//  Serial.println(F("Enter Startup"));
  startup_mode = true;
  
  set_read_delay(10 * 1000L); // 10 seconds

  startup_timer.start();
}

void done_startup_mode() {
//  Serial.println(F("Leave Startup"));
  startup_mode = false;

  if (!system_on) {
      turn_system_off();
  }
}

void highlight_status(bool flag, const __FlashStringHelper *tag) {
  if (flag) {
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }
  display.print(tag);
  display.setTextColor(SSD1306_WHITE);
  display.print(F(" "));
}

void highlight_status(bool flag, char *tag) {
  if (flag) {
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }
  display.print(tag);
  display.setTextColor(SSD1306_WHITE);
  display.print(F(" "));
}

void read_load_cell() {
//  Serial.println(F("Read Cell"));
  if (scale_found) {
    scale.power_up();
    float weight = scale.get_units(10);
    
    if (weight > 20.0) {
      if (!occupied) {
        occupied = true;
        set_read_delay(DEFAULT_READ_TEMP_TIMER);
      }
    } else {
      if (occupied) {
        occupied = false;
        set_read_delay(10 * 60 * 1000L); // 10 minutes
      }
    }
    scale.power_down();
  }
}

void read_temps() {
  digitalWrite(LED_BUILTIN, HIGH);
//  Serial.println(F("Read Temp"));

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

void set_default_heat_cycle() {
  active_delay = DEFAULT_HEAT_TIME;
  heat_on_timer.setDelay(active_delay);

  recycle_delay = DEFAULT_RECYCLE_DELAY;
  heat_reset_timer.setDelay(recycle_delay);
}

void set_unoccupied_heat_cycle() {
  active_delay = 5 * 60 * 1000L;
  heat_on_timer.setDelay(active_delay);
  
  recycle_delay = 25 * 60 * 1000L;
  heat_reset_timer.setDelay(recycle_delay);
}

void process_temperature() {
  
  if (inside_temp < 25.0) {
    if (!cold_in) {
      cold_in = true;
      active_delay = 55 * 60 * 1000L;
      heat_on_timer.setDelay(active_delay);
      recycle_delay = 5*60*1000L;
      heat_reset_timer.setDelay(recycle_delay);
    }
  } else {
    if (cold_in) {
      set_default_heat_cycle();
      cold_in = false;
      cold_out = false;
    }
  }
  
  if (outside_temp < 35.0) {
    if (!cold_out) {
      cold_out = true;
      active_delay = 50 * 60 * 1000L;
      heat_on_timer.setDelay(active_delay);
      recycle_delay = 10*60*1000L;
      heat_reset_timer.setDelay(recycle_delay);
    }
  } else {
    if (cold_out) {
      set_default_heat_cycle();
      cold_out = false;
    }
  }
}

void process_keep_warm() {
  
  if (outside_temp <= 45.0) {
    set_default_heat_cycle();
  } else {
    set_unoccupied_heat_cycle();
  }

}

void process_state_machine() {
  if (system_on) {
    if (outside_temp >= 60.0) {
      turn_system_off();
    } else {
      if (occupied) {
        process_temperature();
      } else {
        process_keep_warm();
      }
    }
    
  } else {
    if (outside_temp <= 50.0) {
      turn_system_on();
    }
  }
}

void loop() {
  startup_timer.update();
  
  occupancy_timer.update();

  sleep_temp_timer.update();

  heat_on_timer.update();
  
  heat_reset_timer.update();

  if (read_temp_timer.update()) {
    run_state_machine = true;
  }

  if (run_state_machine) {
    process_state_machine();
    run_state_machine = false;
  }
  
  display.clearDisplay();
  display.setCursor(0, 0);     // Start at top-left corner


  // Print out the temperature
  display.print(F("Inside  Temp: ")); 
  display.print(inside_temp, 1); display.println(F(" F."));
  
  display.print(F("Outside Temp: ")); 
  display.print(outside_temp, 1); display.println(F(" F."));
  
  display.println("");
  highlight_status(system_on, F("Sys"));
  highlight_status(occupied, F("Occ"));
  highlight_status(cold_out, F("Out"));
  highlight_status(cold_in, F("In"));
  highlight_status(heat_on, F("Heat"));
  display.println("");
  display.display();
}
