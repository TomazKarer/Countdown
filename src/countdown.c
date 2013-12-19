// Countdown - A simple countdown timer for the Pebble watch.
//
// Written by: Don Krause
// License: Free for anyone to use and modify. This software is offered as is, with no warranty or
//          guarantee of operation or acceptable use. Use of the embedded Open Source Font is subject
//          to the license: /resources/src/fonts/OFL.txt
//
//     Features include:
//     - Editable time, in minutes and seconds, max 59:59
//     - Pause and continue timer
//     - Visual and vibration notification when time expires
//     - Reset to run same time again
//     - Button images that change in context with the mode and operation
//
//     Operation:
//       - Countdown initializes in run mode with the timer set to 1 minute
//       - Select Long Click alternates between run mode and edit mode, mode cannot be
//         be changed while the timer is running
//       - In edit mode:
//         - Up button increments the value being edited, it wraps around
//           59->0, press and hold to accelerate.
//         - Select short click alternates between editing minutes and seconds
//         - Down button decrements the value being edited, it wraps around
//           0->59, press and hold to accelerate.
//       - In run mode, when the timer is not running:
//         - Up starts the timer
//         - Select short click does nothing, long click changes to edit mode
//         - Down resets the timer to the last edited value, and clears "Time's Up" if present
//       - In run mode, when the timer is running:
//         - Up pauses the timer
//         - Select short click does nothing, long click does nothing
//         - Down does nothing
//       - Up and Down long clicks are not implemented

#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"

#define MY_UUID { 0x0D, 0xFE, 0xEB, 0x6B, 0x17, 0x9A, 0x4B, 0x5C, 0x9F, 0x00, 0xAA, 0xD6, 0xB8, 0x1E, 0x8E, 0xB3 }
PBL_APP_INFO(MY_UUID,
             "Countdown", "Don Krause",
             1, 0, /* App version */
             RESOURCE_ID_ICON_IMAGE,
             APP_INFO_STANDARD_APP);

Window window;

// Background image is black with a white stripe along the right side for button labels
BmpContainer background_image;

// Button data. Includes number of buttons (probably won't change ;-), definition of button
// name constants, and the pixel location of the upper left corner of the button's image location.
// Button image files are 16x24, note that the status bar is at the top of the screen which
// takes away 16 pixels. Also, containers for button images and states for whether a button
// image is present
#define NUM_BUTTONS 3
#define UP_BUTTON 0
#define UP_BUTTON_X 128
#define UP_BUTTON_Y 0
#define SELECT_BUTTON 1
#define SELECT_BUTTON_X 128
#define SELECT_BUTTON_Y 68
#define DOWN_BUTTON 2
#define DOWN_BUTTON_X 128
#define DOWN_BUTTON_Y 126
BmpContainer button_image_containers[NUM_BUTTONS];
typedef enum { NOT_PRESENT, PRESENT } ButtonImageState;
typedef struct {
    ButtonImageState state;
    int x;
    int y;
} ButtonData;
ButtonData button_data[NUM_BUTTONS] = {
                                        { NOT_PRESENT, UP_BUTTON_X, UP_BUTTON_Y }, 
                                        { NOT_PRESENT, SELECT_BUTTON_X, SELECT_BUTTON_Y },
                                        { NOT_PRESENT, DOWN_BUTTON_X, DOWN_BUTTON_Y }
                                       };

// Vibration pattern when timer expires
const VibePattern timer_done_vibe = {
  .durations = (uint32_t []) {75, 200, 75, 200, 75, 500,
                              75, 200, 75, 200, 75, 500,
                              75, 200, 75, 200, 75},
  .num_segments = 17
};

// Timer modes are editing seconds, editing minutes and running timer.
// Start in MODE_EDIT_SEC (editing seconds)
typedef enum { MODE_EDIT_SEC, MODE_EDIT_MIN, MODE_RUN } Modes;
Modes current_mode = MODE_RUN;

// Timer defaults to 1 minute at app startup
typedef struct {
    int min;
    int sec;
} CounterData;
CounterData init_val = { 1, 0 };        // Remember counter starting point
CounterData curr_val = { 1, 0 };        // Counter current value
bool timer_running = false;             // Is the timer currently running?
int seconds = 0;                        // Number of seconds in countdown

// Layers to display minutes and seconds, a label for minutes and seconds and a time's up message
TextLayer text_min_layer;               // Layer for displaying minutes
TextLayer text_sec_layer;               // Layer for displaying seconds
TextLayer text_label_layer;             // Layer for displaying 'm' and 's' labels
TextLayer text_times_up_layer;          // Layer for displaying "Time's Up" message

// Convert integers where 0 <= val <= 59 to two digit text
// For now, change to '!!' if val is out-of-bounds. Will look strange on the Pebble.
void itoa ( int val, char *txt ) {

  if (sizeof(txt) < 2) {
    return;
  }

  if ( ( val >= 0 ) && ( val <= 59 ) ) {
    txt[0] = (char) ('0' + (val / 10));
    txt[1] = (char) ('0' + (val % 10));
  } else {
    txt[0] = '!';
    txt[1] = '!';
  }

}

// Redisplay the seconds in the timer. Remember what was there so that the drawing only
// takes place if the seconds have changed since the last time this was called.
void redisplay_sec () {

  static char sec_text[] = "XX";
  static int last_sec = -1;
  if (curr_val.sec != last_sec) {
    itoa(curr_val.sec, sec_text);
    last_sec = curr_val.sec;
    text_layer_set_text(&text_sec_layer, sec_text);
  }

}

// Redisplay the minutes in the timer. Remember what was there so that the drawing only
// takes place if the minutes have changed since the last time this was called
void redisplay_min () {

  static char min_text[] = "XX";
  static int last_min = -1;
  if (curr_val.min != last_min) {
    itoa(curr_val.min, min_text);
    last_min = curr_val.min;
    text_layer_set_text(&text_min_layer, min_text);
  }
  
}

// Redisplay the timer minutes and seconds because a time tick has occurred while
// the timer is running, or the minutes or seconds have been edited
void redisplay_timer () {

  redisplay_min();
  redisplay_sec();
  
}

// Removes the image next to a button, if one is present
void remove_button( int button_id ) {

  if (button_data[button_id].state == PRESENT) {
    layer_remove_from_parent(&button_image_containers[button_id].layer.layer);
    bmp_deinit_container(&button_image_containers[button_id]);
    button_data[button_id].state = NOT_PRESENT;
  }
  
}

// Displays an image next to a button. If there is an existing image, remove it first
void display_button ( int button_id, int res_id ) {

  remove_button(button_id);
  button_data[button_id].state = PRESENT;
  bmp_init_container(res_id, &button_image_containers[button_id]);
  button_image_containers[button_id].layer.layer.frame.origin.x = button_data[button_id].x;
  button_image_containers[button_id].layer.layer.frame.origin.y = button_data[button_id].y;
  layer_add_child(&window.layer, &button_image_containers[button_id].layer.layer);

}

// Handle a press of the up button.
void up_single_click_handler (ClickRecognizerRef recognizer, Window *window) {

  (void)recognizer;
  (void)window;
  
  switch (current_mode) {
    case MODE_EDIT_SEC:
      // Increment seconds, wrap to 0 after 59
      init_val.sec = init_val.sec == 59 ? 0 : init_val.sec + 1;
      curr_val.sec = init_val.sec;
      redisplay_sec();
      break;
    case MODE_EDIT_MIN:
      // Increment minutes, wrap to 0 after 59
      init_val.min = init_val.min == 59 ? 0 : init_val.min + 1;
      curr_val.min = init_val.min;
      redisplay_min();
      break;
    case MODE_RUN:
      if (!timer_running) {                                          // Start the timer
        seconds = (curr_val.min * 60) + curr_val.sec;
        if (seconds != 0) {
          display_button(UP_BUTTON, RESOURCE_ID_PAUSE_IMAGE);
          remove_button(DOWN_BUTTON);
          seconds = (curr_val.min * 60) + curr_val.sec;
        }
        timer_running = (seconds != 0);
      } else {                                                       // Pause the timer
        timer_running = false;
        display_button(UP_BUTTON, RESOURCE_ID_START_IMAGE);
        display_button(DOWN_BUTTON, RESOURCE_ID_RESET_IMAGE);
      }
      break;
    default:
      break;
  } // end switch

}

void select_single_click_handler (ClickRecognizerRef recognizer, Window *window) {

  (void)recognizer;
  (void)window;
  
  switch (current_mode) {
    case MODE_EDIT_SEC:
      // Change to editing minutes, unhighlight seconds, highlight minutes
      current_mode = MODE_EDIT_MIN;
      text_layer_set_text_color(&text_sec_layer, GColorWhite);
      text_layer_set_background_color(&text_sec_layer, GColorBlack);
      text_layer_set_text_color(&text_min_layer, GColorBlack);
      text_layer_set_background_color(&text_min_layer, GColorWhite);      
      break;
    case MODE_EDIT_MIN:
      // Change to editing seconds, highlight seconds, unhighlight minutes
      current_mode = MODE_EDIT_SEC;
      text_layer_set_text_color(&text_sec_layer, GColorBlack);
      text_layer_set_background_color(&text_sec_layer, GColorWhite);
      text_layer_set_text_color(&text_min_layer, GColorWhite);
      text_layer_set_background_color(&text_min_layer, GColorBlack);
      break;
    case MODE_RUN:
	  remove_button(SELECT_BUTTON);
      break;
    default:
      break;
  } // end switch

}

void select_long_click_handler (ClickRecognizerRef recognizer, Window *window) {

  (void)recognizer;
  (void)window;
  
  switch (current_mode) {
  
    case MODE_EDIT_SEC:
      current_mode = MODE_RUN;
      text_layer_set_text_color(&text_sec_layer, GColorWhite);
      text_layer_set_background_color(&text_sec_layer, GColorBlack);
      display_button(UP_BUTTON, RESOURCE_ID_START_IMAGE);
      remove_button(SELECT_BUTTON);
      display_button(DOWN_BUTTON, RESOURCE_ID_RESET_IMAGE);
      break;
    case MODE_EDIT_MIN:
      current_mode = MODE_RUN;
      text_layer_set_text_color(&text_min_layer, GColorWhite);
      text_layer_set_background_color(&text_min_layer, GColorBlack);
      display_button(UP_BUTTON, RESOURCE_ID_START_IMAGE);
      remove_button(SELECT_BUTTON);
      display_button(DOWN_BUTTON, RESOURCE_ID_RESET_IMAGE);
      redisplay_timer();
      break;
    case MODE_RUN:
      if (!timer_running) {
        current_mode = MODE_EDIT_MIN;
        text_layer_set_background_color(&text_times_up_layer, GColorBlack); // Clear "Time's Up"
        text_layer_set_text_color(&text_min_layer, GColorBlack);
        text_layer_set_background_color(&text_min_layer, GColorWhite);
        curr_val.sec = init_val.sec;
        curr_val.min = init_val.min;
        display_button(UP_BUTTON, RESOURCE_ID_PLUS_IMAGE);
        display_button(SELECT_BUTTON, RESOURCE_ID_MODE_IMAGE);
        display_button(DOWN_BUTTON, RESOURCE_ID_MINUS_IMAGE);
        redisplay_timer();
      }
      break;
    default:
      break;
  
  }
  
}

// Handle a press of the down button
void down_single_click_handler (ClickRecognizerRef recognizer, Window *window) {

  (void)recognizer;
  (void)window;
  
  text_layer_set_background_color(&text_times_up_layer, GColorBlack); // Clear "Time's Up"
  switch (current_mode) {
    case MODE_EDIT_SEC:
      // Decrement seconds, wrap to 59 after 0
      init_val.sec = init_val.sec == 0 ? 59 : init_val.sec - 1;
      curr_val.sec = init_val.sec;
      redisplay_sec();
      break;
    case MODE_EDIT_MIN:
      // Decrement minutes, wrap to 59 after 0
      init_val.min = init_val.min == 0 ? 59 : init_val.min - 1;
      curr_val.min = init_val.min;
      redisplay_min();
      break;
    case MODE_RUN:
      if (!timer_running) {                                   // Reset the timer to the start value
        curr_val.sec = init_val.sec;
        curr_val.min = init_val.min;
        if ( ( curr_val.sec + curr_val.min ) != 0 ) {         // Only display start button if non-zero
          display_button(UP_BUTTON, RESOURCE_ID_START_IMAGE);
        }
        redisplay_timer();
      } else {                                                // Timer is running, no action
      }
      break;
    default:
      break;
  } // end switch

}

// Set up button click handlers
void click_config_provider(ClickConfig **config, Window *window) {

  (void)window;

  config[BUTTON_ID_SELECT]->click.handler = (ClickHandler) select_single_click_handler;
  config[BUTTON_ID_SELECT]->long_click.handler = (ClickHandler) select_long_click_handler;

  config[BUTTON_ID_UP]->click.handler = (ClickHandler) up_single_click_handler;
  config[BUTTON_ID_UP]->click.repeat_interval_ms = 150;    // Repeats if button held down

  config[BUTTON_ID_DOWN]->click.handler = (ClickHandler) down_single_click_handler;
  config[BUTTON_ID_DOWN]->click.repeat_interval_ms = 150;  // Repeats if button held down
  
}

// Initialization
void handle_init(AppContextRef ctx) {

  (void)ctx;

  window_init(&window, "Countdown");
  window_stack_push(&window, true /* Animated */);
  window_set_background_color(&window, GColorBlack);
  
  resource_init_current_app(&APP_RESOURCES);
  GFont timer_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ISTOKWEB_BOLD_42));
  GFont label_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ISTOKWEB_REGULAR_18));
  GFont times_up_font  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ISTOKWEB_BOLD_28));

  // Background image is all black with a white stripe down the right side for button context images
  bmp_init_container(RESOURCE_ID_BACKGROUND_IMAGE, &background_image);
  layer_add_child(&window.layer, &background_image.layer.layer);

  // Initialize space where minutes are shown
  text_layer_init(&text_min_layer, window.layer.frame);
  text_layer_set_text_color(&text_min_layer, GColorWhite);
  text_layer_set_background_color(&text_min_layer, GColorBlack);
  layer_set_frame(&text_min_layer.layer, GRect(9, 15, 50, 46));
  text_layer_set_font(&text_min_layer, timer_font);
  layer_add_child(&window.layer, &text_min_layer.layer);

  // Initialize space where seconds are shown
  text_layer_init(&text_sec_layer, window.layer.frame);
  text_layer_set_text_color(&text_sec_layer, GColorWhite);
  text_layer_set_background_color(&text_sec_layer, GColorBlack);
  layer_set_frame(&text_sec_layer.layer, GRect(67, 15, 50, 46));
  text_layer_set_font(&text_sec_layer, timer_font);
  layer_add_child(&window.layer, &text_sec_layer.layer);
  
  // Initialize space where the 'm' and 's' labels are shown
  text_layer_init(&text_label_layer, window.layer.frame);
  text_layer_set_text_color(&text_label_layer, GColorWhite);
  text_layer_set_background_color(&text_label_layer, GColorBlack);
  layer_set_frame(&text_label_layer.layer, GRect(2, 64, 115, 18));
  text_layer_set_font(&text_label_layer, label_font);
  layer_add_child(&window.layer, &text_label_layer.layer);
  text_layer_set_text(&text_label_layer, "    m        s");
  
  // Initialize space where the "Time's Up!" message is shown. Displaying
  // is a simple matter of changing the background from black to white
  // so that the text can appear.
  text_layer_init(&text_times_up_layer, GRect(9, 88, 108, 64));
  text_layer_set_text_color(&text_times_up_layer, GColorBlack);
  text_layer_set_background_color(&text_times_up_layer, GColorBlack);
  text_layer_set_font(&text_times_up_layer, times_up_font);
  text_layer_set_text_alignment(&text_times_up_layer, GTextAlignmentCenter);
  layer_add_child(&window.layer, &text_times_up_layer.layer);
  text_layer_set_text(&text_times_up_layer, "Time's\n Up!");
  
  // Display initial button images. Since we start up editing seconds, we need
  // '+', '<-' and '-'
  display_button(UP_BUTTON, RESOURCE_ID_START_IMAGE);
  display_button(DOWN_BUTTON, RESOURCE_ID_RESET_IMAGE);
  redisplay_timer();

  // Attach our desired button functionality
  window_set_click_config_provider(&window, (ClickConfigProvider) click_config_provider);

}

// We have to deinit any BmpContainers that exist when we exit.
void handle_deinit (AppContextRef ctx) {

  (void)ctx;
  
  bmp_deinit_container(&background_image);
  for (int i = 0; i < NUM_BUTTONS; i++) {
    remove_button(i);
  }

}

// Decrement the timer, return true if we hit zero
bool decrement_timer () {

  // Decrement the timer, if at zero then return true, otherwise return false
  // Don't do anything if we are sent a negative number, and set the seconds to zero to stop the timer
  if (seconds > 0 ) {
    seconds--;
    curr_val.min = seconds / 60;
    curr_val.sec = seconds % 60;
    redisplay_timer();
  } else {
    seconds = 0;
  }
  return (seconds == 0);

}

// Decrement the timer every second. When we hit zero we notify the user, both visually
// and with a vibration pattern
// TODO: Can this handler be disabled if the timer isn't running???
void handle_second_tick (AppContextRef ctx, PebbleTickEvent *t) {

  (void) ctx;
  
  // Get out of here quick if the timer isn't running
  if (timer_running) {
    if (decrement_timer()) {
    
      // Time is up, change the background on the "Time's Up" layer to display the message.
      // Redisplay the '<- and reset images, remove the start image
      // Queue up the vibration notification
      text_layer_set_background_color(&text_times_up_layer, GColorWhite);
      timer_running = false;
      remove_button(UP_BUTTON);
      display_button(DOWN_BUTTON, RESOURCE_ID_RESET_IMAGE);
      vibes_enqueue_custom_pattern(timer_done_vibe);
      
    }
  }

}

void pbl_main(void *params) {

  PebbleAppHandlers handlers = {
    .init_handler = &handle_init,
    .deinit_handler = &handle_deinit,
    .tick_info = {
        .tick_handler = &handle_second_tick,
        .tick_units = SECOND_UNIT
    }
  };
  app_event_loop(params, &handlers);
  
}
