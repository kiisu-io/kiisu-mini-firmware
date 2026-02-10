#include <furi_hal_vibro.h>
#include <furi_hal_gpio.h>

#define TAG "FuriHalVibro"

void furi_hal_vibro_init(void) {
    // PA8 repurposed as ADC input for battery voltage measurement (ADC1_IN15)
    // Configure as analog mode (high-impedance) for ADC reading
    furi_hal_gpio_init(&gpio_vibro, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    FURI_LOG_I(TAG, "Init OK (vibro disabled, reused for battery ADC)");
}

void furi_hal_vibro_on(bool value) {
    // Vibro motor not available - PA8 is used for battery ADC
    UNUSED(value);
}
