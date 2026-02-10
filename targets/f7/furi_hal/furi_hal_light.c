#include <core/common_defines.h>
#include <furi_hal_resources.h>
#include <furi_hal_light.h>
#include <stdint.h>

// LP5562 LED driver stubbed out - no I2C LED hardware present
// All functions are no-ops to avoid I2C timeouts on the power bus

void furi_hal_light_init(void) {
    // LP5562 not present - skip I2C initialization
}

void furi_hal_light_set(Light light, uint8_t value) {
    // LP5562 not present - no LEDs to control
    UNUSED(light);
    UNUSED(value);
}

void furi_hal_light_blink_start(Light light, uint8_t brightness, uint16_t on_time, uint16_t period) {
    UNUSED(light);
    UNUSED(brightness);
    UNUSED(on_time);
    UNUSED(period);
}

void furi_hal_light_blink_stop(void) {
}

void furi_hal_light_blink_set_color(Light light) {
    UNUSED(light);
}

void furi_hal_light_sequence(const char* sequence) {
    // Keep delay behavior for timing compatibility, but no light output
    do {
        if(*sequence == '.') {
            furi_delay_ms(250);
        } else if(*sequence == '-') {
            furi_delay_ms(500);
        }
        sequence++;
    } while(*sequence != 0);
}
