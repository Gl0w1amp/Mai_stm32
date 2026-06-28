#include "led_state.h"
#include "led_ws2812.h"
#include "input_snapshot.h"
#include <string.h>

#define NUM_LED 16
#define PRE_BUTTON_LED 2
#define BUTTON_LED_COUNT (NUM_LED / PRE_BUTTON_LED)
#define LED_BOOT_DURATION_MS 1600u
#define LED_EFFECT_STEP_MS 20u
#define LED_HOST_TIMEOUT_MS_DEFAULT 0u
#define LED_HOST_TIMEOUT_MS_MAX 10000u
#define LED_IDLE_BRIGHTNESS_DEFAULT 32u
#define LED_IDLE_EFFECT_BREATHE 0u
#define LED_IDLE_EFFECT_STATIC 1u
#define LED_IDLE_EFFECT_COUNT 2u
#define LED_LOCAL_DEFAULT_MODE LED_MODE_INPUT_REACTIVE

typedef struct {
    uint8_t start[3];
    uint8_t current[3];
    uint8_t target[3];
    uint16_t duration;
    uint16_t elapsed;
} FadeContext;

static uint8_t WS2812_data_button[24];
static uint8_t WS2812_data_billboard[24];

FadeContext fade_ctx[NUM_LED];
static uint16_t fade_pending_duration[NUM_LED];
static uint8_t fade_pending_active[NUM_LED];
static volatile uint8_t led_mode = LED_MODE_BOOT;
static volatile uint8_t led_effective_mode = LED_MODE_BOOT;
static volatile uint8_t led_idle_effect = LED_IDLE_EFFECT_BREATHE;
static volatile uint8_t led_idle_brightness = LED_IDLE_BRIGHTNESS_DEFAULT;
static volatile uint16_t led_host_timeout_ms = LED_HOST_TIMEOUT_MS_DEFAULT;
static volatile uint32_t led_host_deadline_ms = 0u;
static uint32_t led_boot_start_ms = 0u;
static uint32_t led_effect_last_ms = 0u;
static uint8_t led_last_button_bits = 0u;

volatile uint32_t timer7_count = 0;
volatile uint32_t timer7_target = 0;
volatile uint8_t timer7_active = 0;

static void set_led_fade_explicit(uint8_t index,
		uint8_t start_r, uint8_t start_g, uint8_t start_b,
		uint8_t target_r, uint8_t target_g, uint8_t target_b,
		uint8_t speed);
static void led_clear_fades(void);
static uint8_t led_effective_mode_for(uint32_t now, input_snapshot_t *snapshot);
static void led_render_boot(uint32_t now);
static void led_render_idle(uint32_t now);
static void led_render_input_reactive(uint32_t now,
		const input_snapshot_t *snapshot);
static void led_render_off(void);

static uint8_t led_time_reached(uint32_t now, uint32_t deadline)
{
	return (uint8_t)(((int32_t)(now - deadline)) >= 0);
}

static void led_clear_fades(void)
{
	for (uint8_t i = 0u; i < BUTTON_LED_COUNT; i++) {
		fade_ctx[i].duration = 0u;
		fade_ctx[i].elapsed = 0u;
		fade_pending_duration[i] = 0u;
		fade_pending_active[i] = 0u;
	}
}

static uint8_t led_snapshot_button_bits(const input_snapshot_t *snapshot)
{
	if (snapshot == NULL) {
		return 0u;
	}

	return snapshot->button_bits[0];
}

static uint8_t led_effective_mode_for(uint32_t now, input_snapshot_t *snapshot)
{
	uint8_t has_snapshot;
	uint8_t mode = led_mode;
	(void)now;

	if (snapshot != NULL) {
		memset(snapshot, 0, sizeof(*snapshot));
	}
	has_snapshot = input_snapshot_get_latest(snapshot);

	if (mode == LED_MODE_HOST_CONTROLLED) {
		return LED_MODE_HOST_CONTROLLED;
	}
	if (mode == LED_MODE_INPUT_REACTIVE) {
		uint8_t buttons = (has_snapshot != 0u) ?
				led_snapshot_button_bits(snapshot) : 0u;
		if (buttons != 0u) {
			return LED_MODE_INPUT_REACTIVE;
		}
		return LED_MODE_IDLE;
	}
	if (mode == LED_MODE_AUTO) {
		return LED_MODE_IDLE;
	}

	return mode;
}

static void led_render_boot(uint32_t now)
{
	uint32_t elapsed = now - led_boot_start_ms;
	int32_t head_q8;

	if (elapsed >= LED_BOOT_DURATION_MS) {
		(void)LED_SetMode(LED_LOCAL_DEFAULT_MODE, now);
		return;
	}

	head_q8 = (int32_t)(((uint32_t)elapsed *
			(uint32_t)((BUTTON_LED_COUNT + 4u) * 256u)) /
			LED_BOOT_DURATION_MS) - (2 * 256);

	for (uint8_t i = 0u; i < BUTTON_LED_COUNT; i++) {
		int32_t dist = ((int32_t)i * 256) - head_q8;
		uint16_t intensity = 0u;
		uint8_t r;
		uint8_t g;
		uint8_t b;

		if (dist < 0) {
			dist = -dist;
		}

		if (dist < 384) {
			intensity = (uint16_t)(255u - (((uint32_t)dist * 80u) / 384u));
		} else if (dist < 1024) {
			intensity = (uint16_t)(((uint32_t)(1024 - dist) * 175u) / 640u);
		}

		r = (uint8_t)(4u + ((uint32_t)intensity * 116u) / 255u);
		g = (uint8_t)(10u + ((uint32_t)intensity * 170u) / 255u);
		b = (uint8_t)(20u + ((uint32_t)intensity * 235u) / 255u);
		set_led_immediate(i, r, g, b);
	}
	LED_refresh();
}

static void led_render_idle(uint32_t now)
{
	(void)now;

	for (uint8_t i = 0u; i < BUTTON_LED_COUNT; i++) {
		set_led_immediate(i, 0u, 0u, 0u);
	}
	LED_refresh();
}

static void led_render_input_reactive(uint32_t now,
		const input_snapshot_t *snapshot)
{
	uint8_t buttons = led_snapshot_button_bits(snapshot);
	(void)now;

	led_last_button_bits = buttons;

	for (uint8_t i = 0u; i < BUTTON_LED_COUNT; i++) {
		uint8_t active = (uint8_t)((buttons & (uint8_t)(1u << i)) != 0u);

		if (active != 0u) {
			set_led_immediate(i, 180u, 230u, 255u);
		} else {
			set_led_immediate(i, 0u, 0u, 0u);
		}
	}
	LED_refresh();
}

static void led_render_off(void)
{
	for (uint8_t i = 0u; i < BUTTON_LED_COUNT; i++) {
		set_led_immediate(i, 0u, 0u, 0u);
	}
	LED_refresh();
}

void LED_StateMachineInit(uint32_t now)
{
	led_mode = LED_MODE_BOOT;
	led_effective_mode = LED_MODE_BOOT;
	led_host_deadline_ms = 0u;
	led_boot_start_ms = now;
	led_effect_last_ms = now;
	led_last_button_bits = 0u;
	led_clear_fades();
	led_render_boot(now);
}

void LED_ServiceStateMachine(uint32_t now)
{
	uint8_t mode = led_mode;
	uint8_t effective_mode;
	input_snapshot_t snapshot;

	if (mode == LED_MODE_HOST_CONTROLLED) {
		if ((led_host_timeout_ms != 0u) &&
				(led_time_reached(now, led_host_deadline_ms) != 0u)) {
			(void)LED_SetMode(LED_LOCAL_DEFAULT_MODE, now);
			mode = led_mode;
		}
	}

	effective_mode = led_effective_mode_for(now, &snapshot);

	if ((effective_mode == LED_MODE_HOST_CONTROLLED) ||
			(effective_mode == LED_MODE_OFF)) {
		led_effective_mode = effective_mode;
		return;
	}

	if (((uint32_t)(now - led_effect_last_ms) < LED_EFFECT_STEP_MS) &&
			(effective_mode == led_effective_mode)) {
		return;
	}
	led_effect_last_ms = now;
	led_effective_mode = effective_mode;

	if (effective_mode == LED_MODE_INPUT_REACTIVE) {
		led_render_input_reactive(now, &snapshot);
	} else if (effective_mode == LED_MODE_BOOT) {
		led_render_boot(now);
	} else {
		if (mode == LED_MODE_AUTO) {
			led_mode = LED_MODE_IDLE;
		}
		led_render_idle(now);
	}
}

void LED_NotifyHostControl(uint32_t now)
{
	led_mode = LED_MODE_HOST_CONTROLLED;
	led_effective_mode = LED_MODE_HOST_CONTROLLED;
	if (led_host_timeout_ms != 0u) {
		led_host_deadline_ms = now + led_host_timeout_ms;
	} else {
		led_host_deadline_ms = 0u;
	}
}

uint8_t LED_SetMode(uint8_t mode, uint32_t now)
{
	switch (mode) {
	case LED_MODE_AUTO:
		return LED_SetMode(LED_LOCAL_DEFAULT_MODE, now);
	case LED_MODE_IDLE:
		led_mode = LED_MODE_IDLE;
		led_effective_mode = LED_MODE_IDLE;
		led_effect_last_ms = now;
		led_clear_fades();
		led_render_idle(now);
		break;
	case LED_MODE_INPUT_REACTIVE:
		led_mode = LED_MODE_INPUT_REACTIVE;
		led_effective_mode = LED_MODE_IDLE;
		led_effect_last_ms = now;
		led_last_button_bits = 0u;
		led_clear_fades();
		led_render_idle(now);
		break;
	case LED_MODE_HOST_CONTROLLED:
		LED_NotifyHostControl(now);
		break;
	case LED_MODE_OFF:
		led_mode = LED_MODE_OFF;
		led_effective_mode = LED_MODE_OFF;
		led_host_deadline_ms = 0u;
		led_clear_fades();
		led_render_off();
		break;
	case LED_MODE_BOOT:
		led_mode = LED_MODE_BOOT;
		led_effective_mode = LED_MODE_BOOT;
		led_boot_start_ms = now;
		led_effect_last_ms = now;
		led_clear_fades();
		led_render_boot(now);
		break;
	case LED_MODE_DIAGNOSTIC:
	case LED_MODE_ERROR:
		return LED_SetMode(LED_LOCAL_DEFAULT_MODE, now);
	default:
		return 0u;
	}

	return 1u;
}

uint8_t LED_ConfigSet(uint8_t idle_effect, uint8_t idle_brightness,
		uint16_t host_timeout_ms)
{
	if ((idle_effect >= LED_IDLE_EFFECT_COUNT) ||
			(host_timeout_ms > LED_HOST_TIMEOUT_MS_MAX)) {
		return 0u;
	}

	led_idle_effect = idle_effect;
	led_idle_brightness = idle_brightness;
	led_host_timeout_ms = host_timeout_ms;
	return 1u;
}

void LED_StatusSnapshot(LED_Status *status, uint32_t now)
{
	uint16_t remaining = 0u;
	uint8_t mode = led_effective_mode;

	if (status == NULL) {
		return;
	}

	if ((mode == LED_MODE_HOST_CONTROLLED) && (led_host_timeout_ms != 0u) &&
			(led_time_reached(now, led_host_deadline_ms) == 0u)) {
		uint32_t diff = led_host_deadline_ms - now;
		remaining = (diff > UINT16_MAX) ? UINT16_MAX : (uint16_t)diff;
	}

	status->mode = mode;
	status->host_active = (uint8_t)(mode == LED_MODE_HOST_CONTROLLED);
	status->idle_effect = led_idle_effect;
	status->idle_brightness = led_idle_brightness;
	status->host_timeout_ms = led_host_timeout_ms;
	status->host_remaining_ms = remaining;
}

void LED_update_button(uint8_t speed){
	LED_NotifyHostControl(HAL_GetTick());
	for(uint8_t i = 0;i<8;i++){
		set_led_fade(i, WS2812_data_button[i*3], WS2812_data_button[i*3+1], WS2812_data_button[i*3+2], speed);
	}
	if (speed == 0) {
		LED_refresh();
	}
}

void LED_update_button_rgb_speed(const uint8_t *rgb_speed, uint8_t count)
{
	uint8_t immediate_refresh = 0u;

	if (rgb_speed == NULL) {
		return;
	}

	if (count > BUTTON_LED_COUNT) {
		count = BUTTON_LED_COUNT;
	}

	LED_NotifyHostControl(HAL_GetTick());
	for (uint8_t i = 0; i < count; i++) {
		uint8_t r = rgb_speed[i * 4u];
		uint8_t g = rgb_speed[i * 4u + 1u];
		uint8_t b = rgb_speed[i * 4u + 2u];
		uint8_t speed = rgb_speed[i * 4u + 3u];

		WS2812_data_button[i * 3u] = r;
		WS2812_data_button[i * 3u + 1u] = g;
		WS2812_data_button[i * 3u + 2u] = b;
		set_led_fade(i, r, g, b, speed);
		if (speed == 0u) {
			immediate_refresh = 1u;
		}
	}

	if (immediate_refresh != 0u) {
		LED_refresh();
	}
}

void set_led_immediate(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= BUTTON_LED_COUNT) return;
    fade_ctx[index].start[0] = r;
    fade_ctx[index].start[1] = g;
    fade_ctx[index].start[2] = b;
    fade_ctx[index].current[0] = r;
    fade_ctx[index].current[1] = g;
    fade_ctx[index].current[2] = b;
    fade_ctx[index].target[0] = r;
    fade_ctx[index].target[1] = g;
    fade_ctx[index].target[2] = b;
    fade_ctx[index].duration = 0;
    fade_pending_active[index] = 0;
    LED_set(index, r, g, b);
}

void set_led_fade(uint8_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t speed) {
    if (index >= BUTTON_LED_COUNT) return;
    if (speed == 0) {
        set_led_immediate(index, r, g, b);
        return;
    }
    // Start from current color
    memcpy(fade_ctx[index].start, fade_ctx[index].current, 3);
    fade_ctx[index].target[0] = r;
    fade_ctx[index].target[1] = g;
    fade_ctx[index].target[2] = b;
    fade_ctx[index].duration = (4095 / speed * 8);
    fade_ctx[index].elapsed = 0;
    fade_pending_active[index] = 0;
}

static void set_led_fade_explicit(uint8_t index,
		uint8_t start_r, uint8_t start_g, uint8_t start_b,
		uint8_t target_r, uint8_t target_g, uint8_t target_b,
		uint8_t speed)
{
    if (index >= BUTTON_LED_COUNT) return;
    if (speed == 0) {
        set_led_immediate(index, target_r, target_g, target_b);
        return;
    }

    fade_ctx[index].start[0] = start_r;
    fade_ctx[index].start[1] = start_g;
    fade_ctx[index].start[2] = start_b;
    fade_ctx[index].current[0] = start_r;
    fade_ctx[index].current[1] = start_g;
    fade_ctx[index].current[2] = start_b;
    fade_ctx[index].target[0] = target_r;
    fade_ctx[index].target[1] = target_g;
    fade_ctx[index].target[2] = target_b;
    fade_ctx[index].duration = (4095 / speed * 8);
    fade_ctx[index].elapsed = 0;
    fade_pending_active[index] = 0;
    LED_set(index, start_r, start_g, start_b);
}

void LED_update_button_rgb_fade(const uint8_t *rgb_fade, uint8_t count)
{
	if (rgb_fade == NULL) {
		return;
	}

	if (count > BUTTON_LED_COUNT) {
		count = BUTTON_LED_COUNT;
	}

	LED_NotifyHostControl(HAL_GetTick());
	for (uint8_t i = 0; i < count; i++) {
		uint8_t offset = i * 7u;
		uint8_t start_r = rgb_fade[offset];
		uint8_t start_g = rgb_fade[offset + 1u];
		uint8_t start_b = rgb_fade[offset + 2u];
		uint8_t target_r = rgb_fade[offset + 3u];
		uint8_t target_g = rgb_fade[offset + 4u];
		uint8_t target_b = rgb_fade[offset + 5u];
		uint8_t speed = rgb_fade[offset + 6u];

		WS2812_data_button[i * 3u] = target_r;
		WS2812_data_button[i * 3u + 1u] = target_g;
		WS2812_data_button[i * 3u + 2u] = target_b;
		set_led_fade_explicit(i, start_r, start_g, start_b,
				target_r, target_g, target_b, speed);
	}

	if (count != 0u) {
		LED_refresh();
	}
}

void LED_SetButtonFrame(const uint8_t rgb[24]) {
	if (rgb == NULL) {
		return;
	}
	memcpy(WS2812_data_button, rgb, 24);
}

void LED_SetBillboardFrame(const uint8_t rgb[24]) {
	if (rgb == NULL) {
		return;
	}
	memcpy(WS2812_data_billboard, rgb, 24);
}

uint8_t resolve_multi_len(uint8_t start, uint8_t end_field) {
    uint8_t count = end_field;
    if (count == 0x20) {
        count = BUTTON_LED_COUNT; // host shortcut for "all"
    }
    if (start >= BUTTON_LED_COUNT) {
        return 0;
    }
    if (start + count > BUTTON_LED_COUNT) {
        count = BUTTON_LED_COUNT - start;
    }
    return count;
}

void schedule_led_fade(uint8_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t speed) {
    if (index >= BUTTON_LED_COUNT) return;
    if (speed == 0) {
        set_led_immediate(index, r, g, b);
        return;
    }
    fade_ctx[index].target[0] = r;
    fade_ctx[index].target[1] = g;
    fade_ctx[index].target[2] = b;
    fade_pending_duration[index] = (4095 / speed * 8);
    fade_pending_active[index] = 1;
}

void start_pending_fades(void) {
    for (uint8_t i = 0; i < BUTTON_LED_COUNT; i++) {
        if (fade_pending_active[i]) {
            fade_ctx[i].start[0] = fade_ctx[i].current[0];
            fade_ctx[i].start[1] = fade_ctx[i].current[1];
            fade_ctx[i].start[2] = fade_ctx[i].current[2];
            fade_ctx[i].elapsed = 0;
            fade_ctx[i].duration = fade_pending_duration[i];
            fade_pending_active[i] = 0;
        }
    }
}

void LED_ServiceFade(void){
	uint8_t changed = 0u;

    for(int i=0; i<BUTTON_LED_COUNT; i++) {
        if(fade_ctx[i].duration > 0) {
            changed = 1u;
            fade_ctx[i].elapsed++;
            if(fade_ctx[i].elapsed >= fade_ctx[i].duration) {
                fade_ctx[i].elapsed = fade_ctx[i].duration;
                memcpy(fade_ctx[i].current, fade_ctx[i].target, 3);
                fade_ctx[i].duration = 0;
            } else {
                // Integer Lerp
                for(int c=0; c<3; c++) {
                    int32_t start = fade_ctx[i].start[c];
                    int32_t target = fade_ctx[i].target[c];
                    fade_ctx[i].current[c] = start + (target - start) * fade_ctx[i].elapsed / fade_ctx[i].duration;
                }
            }
            LED_set(i, fade_ctx[i].current[0], fade_ctx[i].current[1], fade_ctx[i].current[2]);
        }
    }
    if (changed != 0u) {
		LED_refresh();
    }
}
