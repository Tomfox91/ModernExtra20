#include "pebble.h"
#include "tx_generated.h"

static Window* window;
static GBitmap *background_image_container;

static Layer *minute_display_layer;
static Layer *hour_display_layer;
static Layer *center_display_layer;
static TextLayer *date_layer;
static char date_text[] = "Xxx 88 Xxx";
static TextLayer *sunset_layer;
static char sunset_text[] = "88:88";
static bool bt_ok = false;
static uint8_t battery_level;
static bool battery_plugged;

static GBitmap *icon_battery;
static GBitmap *icon_battery_charge;
static GBitmap *icon_bt;

static Layer *battery_layer;
static Layer *bt_layer;

#define GRECT_FULL_WINDOW GRect(0,0,144,168)

#ifdef INVERSE
static InverterLayer *full_inverse_layer;
#endif

static Layer *background_layer;
static Layer *window_layer;

const GPathInfo MINUTE_HAND_PATH_POINTS = { 4, (GPoint[] ) { { -4, 15 },
				{ 4, 15 }, { 4, -70 }, { -4, -70 }, } };

const GPathInfo HOUR_HAND_PATH_POINTS = { 4, (GPoint[] ) { { -4, 15 },
				{ 4, 15 }, { 4, -50 }, { -4, -50 }, } };

static GPath *hour_hand_path;
static GPath *minute_hand_path;

static AppTimer *timer_handle;
#define COOKIE_MY_TIMER 1
static int my_cookie = COOKIE_MY_TIMER;
#define ANIM_IDLE 0
#define ANIM_START 1
#define ANIM_HOURS 2
#define ANIM_MINUTES 3
#define ANIM_DONE 5
int init_anim = ANIM_DONE;
int32_t second_angle_anim = 0;
unsigned int minute_angle_anim = 0;
unsigned int hour_angle_anim = 0;

void handle_timer(void* vdata) {

	int *data = (int *) vdata;

	if (*data == my_cookie) {
		if (init_anim == ANIM_START) {
			init_anim = ANIM_HOURS;
			timer_handle = app_timer_register(50 /* milliseconds */,
					&handle_timer, &my_cookie);
		} else if (init_anim == ANIM_HOURS) {
			layer_mark_dirty(hour_display_layer);
			timer_handle = app_timer_register(50 /* milliseconds */,
					&handle_timer, &my_cookie);
		} else if (init_anim == ANIM_MINUTES) {
			layer_mark_dirty(minute_display_layer);
			timer_handle = app_timer_register(50 /* milliseconds */,
					&handle_timer, &my_cookie);
		}
	}

}

void center_display_layer_update_callback(Layer *me, GContext* ctx) {
	(void) me;

	GPoint center = grect_center_point(&GRECT_FULL_WINDOW);
	graphics_context_set_fill_color(ctx, GColorBlack);
	graphics_fill_circle(ctx, center, 4);
	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_fill_circle(ctx, center, 3);
}

void minute_display_layer_update_callback(Layer *me, GContext* ctx) {
	(void) me;

	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	unsigned int angle = t->tm_min * 6 + t->tm_sec / 10;

	if (init_anim < ANIM_MINUTES) {
		angle = 0;
	} else if (init_anim == ANIM_MINUTES) {
		minute_angle_anim += 6;
		init_anim = ANIM_DONE;
	}

	gpath_rotate_to(minute_hand_path, (TRIG_MAX_ANGLE / 360) * angle);

	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_context_set_stroke_color(ctx, GColorBlack);

	gpath_draw_filled(ctx, minute_hand_path);
	gpath_draw_outline(ctx, minute_hand_path);
}

void hour_display_layer_update_callback(Layer *me, GContext* ctx) {
	(void) me;

	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	unsigned int angle = t->tm_hour * 30 + t->tm_min / 2;

	if (init_anim < ANIM_HOURS) {
		angle = 0;
	} else if (init_anim == ANIM_HOURS) {
		if (hour_angle_anim == 0 && t->tm_hour >= 12) {
			hour_angle_anim = 360;
		}
		hour_angle_anim += 6;
		if (hour_angle_anim >= angle) {
			init_anim = ANIM_MINUTES;
		} else {
			angle = hour_angle_anim;
		}
	}

	gpath_rotate_to(hour_hand_path, (TRIG_MAX_ANGLE / 360) * angle);

	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_context_set_stroke_color(ctx, GColorBlack);

	gpath_draw_filled(ctx, hour_hand_path);
	gpath_draw_outline(ctx, hour_hand_path);
}

void draw_date() {
	time_t now = time(NULL);
	struct tm* t = localtime(&now);
	char* weekdays[] = {"dom", "lun", "mar", "mer", "gio", "ven", "sab"};
	char* months[] = {"gen", "feb", "mar", "apr", "mag", "giu", "lug", "ago", "set", "ott", "nov", "dic"};

	snprintf(date_text, sizeof(date_text), "%s %d %s", weekdays[t->tm_wday], t->tm_mday, months[t->tm_mon]);
	text_layer_set_text(date_layer, date_text);
}

void draw_sunset() {
	text_layer_set_text(sunset_layer, sunset_text);
}

/*
 * Battery icon callback handler
 */
void battery_layer_update_callback(Layer *layer, GContext *ctx) {

  graphics_context_set_compositing_mode(ctx, GCompOpAssign);

  if (!battery_plugged) {
    graphics_draw_bitmap_in_rect(ctx, icon_battery, GRect(0, 0, 24, 12));
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(7, 4, (uint8_t)((battery_level / 100.0) * 11.0), 4), 0, GCornerNone);
  } else {
    graphics_draw_bitmap_in_rect(ctx, icon_battery_charge, GRect(0, 0, 24, 12));
  }
}



void battery_state_handler(BatteryChargeState charge) {
	battery_level = charge.charge_percent;
	battery_plugged = charge.is_plugged;
	layer_mark_dirty(battery_layer);
}

/*
 * Bluetooth icon callback handler
 */
void bt_layer_update_callback(Layer *layer, GContext *ctx) {
  if (bt_ok)
  	graphics_context_set_compositing_mode(ctx, GCompOpAssign);
  else
  	graphics_context_set_compositing_mode(ctx, GCompOpClear);
  graphics_draw_bitmap_in_rect(ctx, icon_bt, GRect(0, 0, 9, 12));
}

void bt_connection_handler(bool connected) {
	bt_ok = connected;
	layer_mark_dirty(bt_layer);
}

void draw_background_callback(Layer *layer, GContext *ctx) {
	graphics_context_set_compositing_mode(ctx, GCompOpAssign);
	graphics_draw_bitmap_in_rect(ctx, background_image_container,
			GRECT_FULL_WINDOW);
}

void init() {

	// Window
	window = window_create();
	window_stack_push(window, true /* Animated */);
	window_layer = window_get_root_layer(window);

	// Background image
	background_image_container = gbitmap_create_with_resource(
			RESOURCE_ID_IMAGE_BACKGROUND);
	background_layer = layer_create(GRECT_FULL_WINDOW);
	layer_set_update_proc(background_layer, &draw_background_callback);
	layer_add_child(window_layer, background_layer);

	// Date/sunset setup
	date_layer = text_layer_create(GRect(27, 98, 90, 21));
	text_layer_set_text_color(date_layer, GColorWhite);
	text_layer_set_text_alignment(date_layer, GTextAlignmentCenter);
	text_layer_set_background_color(date_layer, GColorClear);
	text_layer_set_font(date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
	layer_add_child(window_layer, text_layer_get_layer(date_layer));
	draw_date();

	sunset_layer = text_layer_create(GRect(27, 118, 90, 17));
	text_layer_set_text_color(sunset_layer, GColorWhite);
	text_layer_set_text_alignment(sunset_layer, GTextAlignmentCenter);
	text_layer_set_background_color(sunset_layer, GColorClear);
	text_layer_set_font(sunset_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
	layer_add_child(window_layer, text_layer_get_layer(sunset_layer));

	// Status setup
	icon_battery = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_ICON);
	icon_battery_charge = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_CHARGE);
	icon_bt = gbitmap_create_with_resource(RESOURCE_ID_BLUETOOTH);

	BatteryChargeState initial = battery_state_service_peek();
	battery_level = initial.charge_percent;
	battery_plugged = initial.is_plugged;
	battery_layer = layer_create(GRect(50,56,24,12)); //24*12
	layer_set_update_proc(battery_layer, &battery_layer_update_callback);
	layer_add_child(window_layer, battery_layer);


	bt_ok = bluetooth_connection_service_peek();
	bt_layer = layer_create(GRect(83,56,9,12)); //9*12
	layer_set_update_proc(bt_layer, &bt_layer_update_callback);
	layer_add_child(window_layer, bt_layer);

	// Hands setup
	hour_display_layer = layer_create(GRECT_FULL_WINDOW);
	layer_set_update_proc(hour_display_layer,
			&hour_display_layer_update_callback);
	layer_add_child(window_layer, hour_display_layer);

	hour_hand_path = gpath_create(&HOUR_HAND_PATH_POINTS);
	gpath_move_to(hour_hand_path, grect_center_point(&GRECT_FULL_WINDOW));

	minute_display_layer = layer_create(GRECT_FULL_WINDOW);
	layer_set_update_proc(minute_display_layer,
			&minute_display_layer_update_callback);
	layer_add_child(window_layer, minute_display_layer);

	minute_hand_path = gpath_create(&MINUTE_HAND_PATH_POINTS);
	gpath_move_to(minute_hand_path, grect_center_point(&GRECT_FULL_WINDOW));

	center_display_layer = layer_create(GRECT_FULL_WINDOW);
	layer_set_update_proc(center_display_layer,
			&center_display_layer_update_callback);
	layer_add_child(window_layer, center_display_layer);

	// Configurable inverse
#ifdef INVERSE
	full_inverse_layer = inverter_layer_create(GRECT_FULL_WINDOW);
	layer_add_child(window_layer, inverter_layer_get_layer(full_inverse_layer));
#endif

}

void deinit() {

	gbitmap_destroy(background_image_container);
	gbitmap_destroy(icon_battery);
	gbitmap_destroy(icon_battery_charge);
	gbitmap_destroy(icon_bt);
	text_layer_destroy(date_layer);
	layer_destroy(minute_display_layer);
	layer_destroy(hour_display_layer);
	layer_destroy(center_display_layer);
	layer_destroy(battery_layer);
	layer_destroy(bt_layer);

#ifdef INVERSE
	inverter_layer_destroy(full_inverse_layer);
#endif

	layer_destroy(background_layer);
	gpath_destroy(hour_hand_path);
	gpath_destroy(minute_hand_path);

	layer_destroy(window_layer);
}

void handle_tick(struct tm *tick_time, TimeUnits units_changed) {

	if (init_anim == ANIM_IDLE) {
		init_anim = ANIM_START;
		timer_handle = app_timer_register(50 /* milliseconds */, &handle_timer,
				&my_cookie);
	} else if (init_anim == ANIM_DONE) {
		if (tick_time->tm_sec % 10 == 0) {
			layer_mark_dirty(minute_display_layer);

			if (tick_time->tm_sec == 0) {
				if (tick_time->tm_min % 2 == 0) {
					layer_mark_dirty(hour_display_layer);
					if (tick_time->tm_min == 0 && tick_time->tm_hour == 0) {
						draw_date();
					}
				}
			}
		}
	}
}


static void in_received_handler(DictionaryIterator *received, void * q) {
	Tuple* tsucc = dict_find(received, CODE_SUNSET_SUCCESS);
	Tuple* ttime = dict_find(received, CODE_SUNSET_TIME);

	if (tsucc->value->int32) {
		strncpy(sunset_text, ttime->value->cstring, sizeof(sunset_text));
	} else {
		strncpy(sunset_text, "ERR", sizeof(sunset_text));
	}
	draw_sunset();
}

static void init_mess() {
	app_message_open(app_message_inbox_size_maximum(), 0);
	app_message_register_inbox_received(&in_received_handler);
}

static void deinit_mess() {
	app_message_deregister_callbacks();
}


/*
 * Main - or main as it is known
 */
int main(void) {
	init();
	init_mess();
	tick_timer_service_subscribe(MINUTE_UNIT, &handle_tick);
	bluetooth_connection_service_subscribe(&bt_connection_handler);
	battery_state_service_subscribe(&battery_state_handler);

	app_event_loop();
	deinit_mess();
	deinit();
}

