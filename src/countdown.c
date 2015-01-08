// Countdown - A simple countdown timer for the Pebble watch.
//
// Written by: Don Krause
// License: Free for anyone to use and modify. This software is offered as is, with no warranty or
//          guarantee of operation or acceptable use. Use of the embedded Open Source Font is subject
//          to the license: /resources/src/fonts/OFL.txt
//
//  2.0: Updated to SDK 2.0. Greatly simplified, uses MenuBarLayer. Removed custom fonts. Smaller
//       more efficient.
//
//  1.0: Features include:
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

#include <pebble.h>

Window *window;

// Action Bar
ActionBarLayer *action_bar_layer;

// Button data, based on the enum ButtonId in pebble.h. State (PRESENT/NOT PRESENT), bitmap
// if the button is PRESENT
typedef enum { NOT_PRESENT, PRESENT } ButtonImageState;
typedef struct {
    ButtonImageState state;
    GBitmap *bitmap;
} ButtonData;
ButtonData button_data[NUM_BUTTONS] = {
                                        { NOT_PRESENT, NULL }, 
                                        { NOT_PRESENT, NULL }, 
                                        { NOT_PRESENT, NULL },
                                        { NOT_PRESENT, NULL }
                                       };

// Vibration pattern when timer expires
const VibePattern timer_done_vibe = {
  .durations = (uint32_t []) {75, 200, 75, 200, 75, 500,
                              75, 200, 75, 200, 75, 500,
                              75, 200, 75, 200, 75},
  .num_segments = 17
};

// Timer modes are editing seconds, editing minutes and running timer. Select button changes modes
// via both short and long clicks.
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
TextLayer *text_min_layer;               // Layer for displaying minutes
TextLayer *text_sec_layer;               // Layer for displaying seconds
TextLayer *text_label_layer;             // Layer for displaying 'm' and 's' labels
TextLayer *text_times_up_layer;          // Layer for displaying "Time's Up" message

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

  APP_LOG(APP_LOG_LEVEL_DEBUG, "enter:redisplay_sec()");
  static char sec_text[] = "XX";
  static int last_sec = -1;
  if (curr_val.sec != last_sec) {
    itoa(curr_val.sec, sec_text);
    last_sec = curr_val.sec;
    text_layer_set_text(text_sec_layer, sec_text);
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "exit:redisplay_sec()");

}

// Redisplay the minutes in the timer. Remember what was there so that the drawing only
// takes place if the minutes have changed since the last time this was called
void redisplay_min () {

  APP_LOG(APP_LOG_LEVEL_DEBUG, "enter:redisplay_min()");
  static char min_text[] = "XX";
  static int last_min = -1;
  if (curr_val.min != last_min) {
    itoa(curr_val.min, min_text);
    last_min = curr_val.min;
    text_layer_set_text(text_min_layer, min_text);
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "exit:redisplay_min()");
  
}

// Redisplay the timer minutes and seconds because a time tick has occurred while
// the timer is running, or the minutes or seconds have been edited
void redisplay_timer () {

  APP_LOG(APP_LOG_LEVEL_DEBUG, "enter:redisplay_timer()");
  redisplay_min();
  redisplay_sec();
  APP_LOG(APP_LOG_LEVEL_DEBUG, "exit:redisplay_timer()");
  
}

// Removes the image next to a button, if one is present
void remove_button( ButtonId button_id ) {

  APP_LOG(APP_LOG_LEVEL_DEBUG, "enter:remove_button(), button:%2d", button_id);
  if (button_data[button_id].state == PRESENT) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Removing button: %2d", button_id);
    action_bar_layer_clear_icon(action_bar_layer, button_id);
    gbitmap_destroy(button_data[button_id].bitmap);
    button_data[button_id].bitmap = NULL;
    button_data[button_id].state = NOT_PRESENT;
  } else {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "No button: %2d to remove", button_id);
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "exit:remove_button()");
  
}

// Displays an image next to a button. If there is an existing image, remove it first
void display_button ( ButtonId button_id, uint32_t res_id ) {

  APP_LOG(APP_LOG_LEVEL_DEBUG, "enter:display_button()");
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Adding button: %2d", button_id);
  remove_button(button_id);
  button_data[button_id].state = PRESENT;
  button_data[button_id].bitmap = gbitmap_create_with_resource(res_id);
  action_bar_layer_set_icon(action_bar_layer, button_id, button_data[button_id].bitmap);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "display_button: button_id:%2d, bitmap:%p, layer:%p",
          button_id, button_data[button_id].bitmap, action_bar_layer);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "exit:display_button()");

}

// Handle a press of the up button.
static void up_single_click_handler (ClickRecognizerRef recognizer, void *ctx) {

  APP_LOG(APP_LOG_LEVEL_DEBUG, "enter:up_single_click_handler()");
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
          display_button(BUTTON_ID_UP, RESOURCE_ID_PAUSE_IMAGE);
          remove_button(BUTTON_ID_SELECT);
          remove_button(BUTTON_ID_DOWN);
          seconds = (curr_val.min * 60) + curr_val.sec;
        }
        timer_running = (seconds != 0);
      } else {                                                       // Pause the timer
        timer_running = false;
        display_button(BUTTON_ID_UP, RESOURCE_ID_START_IMAGE);
        display_button(BUTTON_ID_SELECT, RESOURCE_ID_MODE_IMAGE);
        display_button(BUTTON_ID_DOWN, RESOURCE_ID_RESET_IMAGE);
      }
      break;
    default:
      break;
  } // end switch
  APP_LOG(APP_LOG_LEVEL_DEBUG, "exit:up_single_click_handler()");

}

static void select_single_click_handler (ClickRecognizerRef recognizer, void *ctx) {
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "enter:select_single_click_handler()");
  switch (current_mode) {
    case MODE_EDIT_SEC:
      // Change to editing minutes, unhighlight seconds, highlight minutes
      current_mode = MODE_EDIT_MIN;
      text_layer_set_text_color(text_sec_layer, GColorWhite);
      text_layer_set_background_color(text_sec_layer, GColorBlack);
      text_layer_set_text_color(text_min_layer, GColorBlack);
      text_layer_set_background_color(text_min_layer, GColorWhite);      
      break;
    case MODE_EDIT_MIN:
      // Change to editing seconds, highlight seconds, unhighlight minutes
      current_mode = MODE_EDIT_SEC;
      text_layer_set_text_color(text_sec_layer, GColorBlack);
      text_layer_set_background_color(text_sec_layer, GColorWhite);
      text_layer_set_text_color(text_min_layer, GColorWhite);
      text_layer_set_background_color(text_min_layer, GColorBlack);
      break;
    case MODE_RUN:
      break;
    default:
      break;
  } // end switch
  APP_LOG(APP_LOG_LEVEL_DEBUG, "exit:select_single_click_handler()");

}

static void select_long_click_handler (ClickRecognizerRef recognizer, void *ctx) {

  APP_LOG(APP_LOG_LEVEL_DEBUG, "enter:select_long_click_handler()");
  switch (current_mode) {
  
    case MODE_EDIT_SEC:
      current_mode = MODE_RUN;
      text_layer_set_text_color(text_sec_layer, GColorWhite);
      text_layer_set_background_color(text_sec_layer, GColorBlack);
      display_button(BUTTON_ID_UP, RESOURCE_ID_START_IMAGE);
      display_button(BUTTON_ID_DOWN, RESOURCE_ID_RESET_IMAGE);
      break;
    case MODE_EDIT_MIN:
      current_mode = MODE_RUN;
      text_layer_set_text_color(text_min_layer, GColorWhite);
      text_layer_set_background_color(text_min_layer, GColorBlack);
      display_button(BUTTON_ID_UP, RESOURCE_ID_START_IMAGE);
      display_button(BUTTON_ID_DOWN, RESOURCE_ID_RESET_IMAGE);
      redisplay_timer();
      break;
    case MODE_RUN:
      if (!timer_running) {
        current_mode = MODE_EDIT_MIN;
        text_layer_set_background_color(text_times_up_layer, GColorBlack); // Clear "Time's Up"
        text_layer_set_text_color(text_min_layer, GColorBlack);
        text_layer_set_background_color(text_min_layer, GColorWhite);
        curr_val.sec = init_val.sec;
        curr_val.min = init_val.min;
        display_button(BUTTON_ID_UP, RESOURCE_ID_PLUS_IMAGE);
        display_button(BUTTON_ID_SELECT, RESOURCE_ID_MODE_IMAGE);
        display_button(BUTTON_ID_DOWN, RESOURCE_ID_MINUS_IMAGE);
        redisplay_timer();
      }
      break;
    default:
      break;
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "exit:select_long_click_handler()");
  
}

// Handle a press of the down button
static void down_single_click_handler (ClickRecognizerRef recognizer, void *ctx) {

  APP_LOG(APP_LOG_LEVEL_DEBUG, "enter:down_single_click_handler()");
  text_layer_set_background_color(text_times_up_layer, GColorBlack); // Clear "Time's Up"
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
          display_button(BUTTON_ID_UP, RESOURCE_ID_START_IMAGE);
          display_button(BUTTON_ID_SELECT, RESOURCE_ID_MODE_IMAGE);
        }
        redisplay_timer();
      } else {                                                // Timer is running, no action
      }
      break;
    default:
      break;
  } // end switch
  APP_LOG(APP_LOG_LEVEL_DEBUG, "exit:down_single_click_handler()");

}

// Set up button click handlers

static void set_click_config_provider(void *ctx) {

  APP_LOG(APP_LOG_LEVEL_DEBUG, "enter:set_click_config_provider()");
  if (button_data[BUTTON_ID_UP].state == PRESENT) {
    window_single_repeating_click_subscribe(BUTTON_ID_UP, 150, up_single_click_handler);
  } else {
    window_single_repeating_click_subscribe(BUTTON_ID_UP, 0, NULL);
  }
  if (button_data[BUTTON_ID_SELECT].state == PRESENT) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_single_click_handler);
    window_long_click_subscribe(BUTTON_ID_SELECT, 1000, select_long_click_handler, NULL);
  } else {
    window_single_click_subscribe(BUTTON_ID_SELECT, NULL);
    window_long_click_subscribe(BUTTON_ID_SELECT, 0, NULL, NULL);
  }
  if (button_data[BUTTON_ID_DOWN].state == PRESENT) {
    window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, down_single_click_handler);
  } else {
    window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 0, NULL);
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "exit:set_click_config_provider()");
  
}


// Decrement the timer, return true if we hit zero
bool decrement_timer () {

  APP_LOG(APP_LOG_LEVEL_DEBUG, "enter:decrement_timer()");
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
  APP_LOG(APP_LOG_LEVEL_DEBUG, "exit:decrement_timer()");
  return (seconds == 0);

}

// Decrement the timer every second. When we hit zero we notify the user, both visually
// and with a vibration pattern
// TODO: Can this handler be disabled if the timer isn't running???
static void handle_second_tick(struct tm *t, TimeUnits units_changed) {

  APP_LOG(APP_LOG_LEVEL_DEBUG, "enter:handle_second_tick()");
  // Get out of here quick if the timer isn't running
  if (timer_running) {
    if (decrement_timer()) {
    
      // Time is up, change the background on the "Time's Up" layer to display the message.
      // Redisplay the '<- and reset images, remove the start image
      // Queue up the vibration notification
      text_layer_set_background_color(text_times_up_layer, GColorWhite);
      timer_running = false;
      remove_button(BUTTON_ID_UP);
      display_button(BUTTON_ID_DOWN, RESOURCE_ID_RESET_IMAGE);
      vibes_enqueue_custom_pattern(timer_done_vibe);
      
      //After notification, there is no need to manually reset timer. To save on one click:
      curr_val.sec = init_val.sec;
      curr_val.min = init_val.min;
      if ( ( curr_val.sec + curr_val.min ) != 0 ) {         // Only display start button if non-zero
	display_button(BUTTON_ID_UP, RESOURCE_ID_START_IMAGE);
	display_button(BUTTON_ID_SELECT, RESOURCE_ID_MODE_IMAGE);
      }
      redisplay_timer();
       text_layer_set_background_color(text_times_up_layer, GColorBlack); // Clear "Time's Up"
      
    }
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "exit:handle_second_tick()");

}


// Initialization
void handle_init(void) {

  APP_LOG(APP_LOG_LEVEL_DEBUG, "enter:handle_init()");

  window = window_create();
  Layer *window_layer = window_get_root_layer(window);
  window_set_background_color(window, GColorBlack);
  
  action_bar_layer = action_bar_layer_create();
  action_bar_layer_set_background_color(action_bar_layer, GColorWhite);
  action_bar_layer_add_to_window(action_bar_layer, window);
  action_bar_layer_set_click_config_provider(action_bar_layer, set_click_config_provider);  
  
  GFont timer_font = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
  GFont label_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  GFont times_up_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);

  // Initialize space where minutes are shown
  text_min_layer = text_layer_create(GRect(9, 15, 50, 46));
  text_layer_set_text_color(text_min_layer, GColorWhite);
  text_layer_set_background_color(text_min_layer, GColorBlack);
  text_layer_set_font(text_min_layer, timer_font);
  text_layer_set_text_alignment(text_min_layer, GTextAlignmentCenter);
  layer_add_child((Layer *)window_layer, (Layer *)text_min_layer);

  // Initialize space where seconds are shown
  text_sec_layer = text_layer_create(GRect(67, 15, 50, 46));
  text_layer_set_text_color(text_sec_layer, GColorWhite);
  text_layer_set_background_color(text_sec_layer, GColorBlack);
  text_layer_set_font(text_sec_layer, timer_font);
  text_layer_set_text_alignment(text_sec_layer, GTextAlignmentCenter);
  layer_add_child((Layer *)window_layer, (Layer *)text_sec_layer);
  
  // Initialize space where the 'm' and 's' labels are shown
  text_label_layer = text_layer_create(GRect(2, 64, 115, 18));
  text_layer_set_text_color(text_label_layer, GColorWhite);
  text_layer_set_background_color(text_label_layer, GColorBlack);
  text_layer_set_font(text_label_layer, label_font);
  layer_add_child((Layer *)window_layer, (Layer *)text_label_layer);
  text_layer_set_text(text_label_layer, "         m              s");
  
  // Initialize space where the "Time's Up!" message is shown. Displaying
  // is a simple matter of changing the background from black to white
  // so that the text can appear.
  text_times_up_layer = text_layer_create(GRect(9, 88, 108, 64));
  text_layer_set_text_color(text_times_up_layer, GColorBlack);
  text_layer_set_background_color(text_times_up_layer, GColorBlack);
  text_layer_set_font(text_times_up_layer, times_up_font);
  text_layer_set_text_alignment(text_times_up_layer, GTextAlignmentCenter);
  layer_add_child((Layer *)window_layer, (Layer *)text_times_up_layer);
  text_layer_set_text(text_times_up_layer, "Time's\n Up!");
  
  // Display initial button images. Since we start up editing seconds, we need
  // '+', '<-' and '-'
  display_button(BUTTON_ID_UP, RESOURCE_ID_START_IMAGE);
  display_button(BUTTON_ID_SELECT, RESOURCE_ID_MODE_IMAGE);
  display_button(BUTTON_ID_DOWN, RESOURCE_ID_RESET_IMAGE);
  redisplay_timer();

  // Subscribe to timer ticks every second
  tick_timer_service_subscribe(SECOND_UNIT, handle_second_tick);

  window_stack_push(window, true /* Animated */);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "exit:handle_init()");

}

// We have to deinit any BmpContainers that exist when we exit.
void handle_deinit() {

  APP_LOG(APP_LOG_LEVEL_DEBUG, "enter:handle_deinit()");
  for (ButtonId i = BUTTON_ID_BACK; i < NUM_BUTTONS; i++) {
    remove_button(i);
  }
  action_bar_layer_destroy(action_bar_layer);
  text_layer_destroy(text_min_layer);
  text_layer_destroy(text_sec_layer);
  text_layer_destroy(text_times_up_layer);
  text_layer_destroy(text_label_layer);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "exit:handle_deinit()");
  
}

int main(void) {

  handle_init();
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Calling app_event_loop");
  app_event_loop();
  handle_deinit();
  
}
