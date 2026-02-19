#include <furi_hal_power.h>
#include <furi_hal_clock.h>
#include <furi_hal_bt.h>
#include <furi_hal_vibro.h>
#include <furi_hal_resources.h>
#include <furi_hal_serial_control.h>
#include <furi_hal_rtc.h>
#include <furi_hal_debug.h>
#include <furi_hal_adc.h>
#include <furi_hal_usb.h>
#include <furi_hal_spi.h>
#include <furi_hal_gpio.h>
#include <furi_hal_version.h>

#include <stm32wbxx_ll_rcc.h>
#include <stm32wbxx_ll_rtc.h>
#include <stm32wbxx_ll_pwr.h>
#include <stm32wbxx_ll_hsem.h>
#include <stm32wbxx_ll_cortex.h>
#include <stm32wbxx_ll_gpio.h>

#include <hsem_map.h>

#include <furi.h>

bool furi_hal_usb_is_connected(void);

#define TAG "FuriHalPower"

const GpioPin gpio_periph_power = {.port = GPIOA, .pin = LL_GPIO_PIN_3};

// Battery ADC configuration
// PA8 = ADC1_IN15, with 1M:330k voltage divider (1:2 ratio)
// Battery voltage range: ~3.0V (dead) to ~4.2V (full)
// At divider output: ~1.5V to ~2.1V
// Using 2.5V ADC reference scale to avoid clipping at 4.2V
#define FURI_HAL_POWER_BATTERY_ADC_CHANNEL FuriHalAdcChannel15
#define FURI_HAL_POWER_BATTERY_VOLTAGE_DIVIDER_RATIO (4.0f)
#define FURI_HAL_POWER_BATTERY_MIN_VOLTAGE_MV (3300) // 0%
#define FURI_HAL_POWER_BATTERY_MAX_VOLTAGE_MV (4200) // 100%
#define FURI_HAL_POWER_BATTERY_CHARGE_DONE_MV (4150) // Charging considered done above this
#define FURI_HAL_POWER_BATTERY_DESIGN_CAPACITY (2100) // mAh

#ifndef FURI_HAL_POWER_DEBUG_WFI_GPIO
#define FURI_HAL_POWER_DEBUG_WFI_GPIO (&gpio_ext_pb2)
#endif

#ifndef FURI_HAL_POWER_DEBUG_STOP_GPIO
#define FURI_HAL_POWER_DEBUG_STOP_GPIO (&gpio_ext_pc3)
#endif

#ifndef FURI_HAL_POWER_STOP_MODE
#define FURI_HAL_POWER_STOP_MODE (LL_PWR_MODE_STOP2)
#endif

typedef struct {
    volatile uint8_t insomnia;
    volatile uint8_t suppress_charge;
} FuriHalPower;

static volatile FuriHalPower furi_hal_power = {
    .insomnia = 0,
    .suppress_charge = 0,
};

// Read battery voltage in millivolts via ADC on PA8 (channel 15)
// PA8 has a 1M:1M voltage divider, so actual battery voltage = ADC reading * 2
static uint16_t furi_hal_power_get_battery_voltage_adc(void) {
    FuriHalAdcHandle* adc = furi_hal_adc_acquire();
    furi_hal_adc_configure_ex(
        adc,
        FuriHalAdcScale2500,
        FuriHalAdcClockSync64,
        FuriHalAdcOversample64,
        FuriHalAdcSamplingtime247_5);
    uint16_t raw = furi_hal_adc_read(adc, FURI_HAL_POWER_BATTERY_ADC_CHANNEL);
    float voltage_at_pin_mv = furi_hal_adc_convert_to_voltage(adc, raw);
    furi_hal_adc_release(adc);
    // Multiply by divider ratio to get actual battery voltage
    return (uint16_t)(voltage_at_pin_mv * FURI_HAL_POWER_BATTERY_VOLTAGE_DIVIDER_RATIO+2700); //fix for km 1a hw error
}

// Convert battery voltage (mV) to percentage using linear approximation
// 3300mV = 0%, 4200mV = 100%
static uint8_t furi_hal_power_voltage_to_pct(uint16_t voltage_mv) {
    if(voltage_mv <= FURI_HAL_POWER_BATTERY_MIN_VOLTAGE_MV) return 0;
    if(voltage_mv >= FURI_HAL_POWER_BATTERY_MAX_VOLTAGE_MV) return 100;

    uint32_t range = FURI_HAL_POWER_BATTERY_MAX_VOLTAGE_MV -
                     FURI_HAL_POWER_BATTERY_MIN_VOLTAGE_MV;
    uint32_t offset = voltage_mv - FURI_HAL_POWER_BATTERY_MIN_VOLTAGE_MV;
    return (uint8_t)((offset * 100) / range);
}

void furi_hal_power_init(void) {
#ifdef FURI_HAL_POWER_DEBUG
    furi_hal_gpio_init_simple(FURI_HAL_POWER_DEBUG_WFI_GPIO, GpioModeOutputPushPull);
    furi_hal_gpio_init_simple(FURI_HAL_POWER_DEBUG_STOP_GPIO, GpioModeOutputPushPull);
    furi_hal_gpio_write(FURI_HAL_POWER_DEBUG_WFI_GPIO, 0);
    furi_hal_gpio_write(FURI_HAL_POWER_DEBUG_STOP_GPIO, 0);
#endif

    LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
    LL_PWR_SMPS_SetMode(LL_PWR_SMPS_STEP_DOWN);

    LL_PWR_SetPowerMode(FURI_HAL_POWER_STOP_MODE);
    LL_C2_PWR_SetPowerMode(FURI_HAL_POWER_STOP_MODE);

#if FURI_HAL_POWER_STOP_MODE == LL_PWR_MODE_STOP0
    LL_RCC_HSI_EnableInStopMode(); // Ensure that MR is capable of work in STOP0
#endif

    // PA8 (vibro pin) is configured as analog input in furi_hal_vibro_init()
    // for battery voltage measurement via ADC1_IN15 with 1M:1M divider

    FURI_LOG_I(TAG, "Init OK");
}

bool furi_hal_power_gauge_is_ok(void) {
    // No BQ27220 fuel gauge - ADC-based measurement is always available
    return true;
}

bool furi_hal_power_is_shutdown_requested(void) {
    // No BQ27220 SYSDWN flag available
    return false;
}

uint16_t furi_hal_power_insomnia_level(void) {
    return furi_hal_power.insomnia;
}

void furi_hal_power_insomnia_enter(void) {
    FURI_CRITICAL_ENTER();
    furi_check(furi_hal_power.insomnia < UINT8_MAX);
    furi_hal_power.insomnia++;
    FURI_CRITICAL_EXIT();
}

void furi_hal_power_insomnia_exit(void) {
    FURI_CRITICAL_ENTER();
    furi_check(furi_hal_power.insomnia > 0);
    furi_hal_power.insomnia--;
    FURI_CRITICAL_EXIT();
}

bool furi_hal_power_sleep_available(void) {
    return furi_hal_power.insomnia == 0;
}

static inline bool furi_hal_power_deep_sleep_available(void) {
    return furi_hal_bt_is_alive() && !furi_hal_rtc_is_flag_set(FuriHalRtcFlagLegacySleep) &&
           !furi_hal_debug_is_gdb_session_active();
}

static inline void furi_hal_power_light_sleep(void) {
#ifdef FURI_HAL_POWER_DEBUG
    furi_hal_gpio_write(FURI_HAL_POWER_DEBUG_WFI_GPIO, 1);
#endif
    __WFI();
#ifdef FURI_HAL_POWER_DEBUG
    furi_hal_gpio_write(FURI_HAL_POWER_DEBUG_WFI_GPIO, 0);
#endif
}

static inline void furi_hal_power_suspend_aux_periphs(void) {
    // Disable USART
    furi_hal_serial_control_suspend();
}

static inline void furi_hal_power_resume_aux_periphs(void) {
    // Re-enable USART
    furi_hal_serial_control_resume();
}

static inline void furi_hal_power_deep_sleep(void) {
    furi_hal_power_suspend_aux_periphs();

    if(!furi_hal_clock_switch_pll2hse()) {
        // Hello core2 my old friend
        return;
    }

    while(LL_HSEM_1StepLock(HSEM, CFG_HW_RCC_SEMID))
        ;

    if(!LL_HSEM_1StepLock(HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID)) {
        if(LL_PWR_IsActiveFlag_C2DS() || LL_PWR_IsActiveFlag_C2SB()) {
            // Release ENTRY_STOP_MODE semaphore
            LL_HSEM_ReleaseLock(HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID, 0);

            // The switch on HSI before entering Stop Mode is required
            furi_hal_clock_switch_hse2hsi();
        }
    } else {
        /**
         * The switch on HSI before entering Stop Mode is required 
         */
        furi_hal_clock_switch_hse2hsi();
    }

    /* Release RCC semaphore */
    LL_HSEM_ReleaseLock(HSEM, CFG_HW_RCC_SEMID, 0);

    // Prepare deep sleep
    LL_LPM_EnableDeepSleep();

#if defined(__CC_ARM)
    // Force store operations
    __force_stores();
#endif

#ifdef FURI_HAL_POWER_DEBUG
    furi_hal_gpio_write(FURI_HAL_POWER_DEBUG_STOP_GPIO, 1);
#endif
    __WFI();
#ifdef FURI_HAL_POWER_DEBUG
    furi_hal_gpio_write(FURI_HAL_POWER_DEBUG_STOP_GPIO, 0);
#endif

    LL_LPM_EnableSleep();

    /* Release ENTRY_STOP_MODE semaphore */
    LL_HSEM_ReleaseLock(HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID, 0);

    while(LL_HSEM_1StepLock(HSEM, CFG_HW_RCC_SEMID))
        ;

    if(LL_RCC_GetSysClkSource() == LL_RCC_SYS_CLKSOURCE_STATUS_HSI) {
        furi_hal_clock_switch_hsi2hse();
    } else {
        // Ensure that we are already on HSE
        furi_check(LL_RCC_GetSysClkSource() == LL_RCC_SYS_CLKSOURCE_STATUS_HSE);
    }

    LL_HSEM_ReleaseLock(HSEM, CFG_HW_RCC_SEMID, 0);

    furi_check(furi_hal_clock_switch_hse2pll());

    furi_hal_power_resume_aux_periphs();
    furi_hal_rtc_sync_shadow();
}

void furi_hal_power_sleep(void) {
    if(furi_hal_power_deep_sleep_available()) {
        furi_hal_power_deep_sleep();
    } else {
        furi_hal_power_light_sleep();
    }
}

uint8_t furi_hal_power_get_pct(void) {
    uint16_t voltage_mv = furi_hal_power_get_battery_voltage_adc();
    return furi_hal_power_voltage_to_pct(voltage_mv);
}

uint8_t furi_hal_power_get_bat_health_pct(void) {
    // No fuel gauge to track battery health - return 100% (unknown)
    return 100;
}

bool furi_hal_power_is_charging(void) {
    // Detect charging via USB connection state (SOF-based)
    return furi_hal_usb_is_connected();
}

bool furi_hal_power_is_charging_done(void) {
    // Consider charging done when USB is connected and battery voltage >= 4.15V
    if(!furi_hal_usb_is_connected()) return false;
    uint16_t voltage_mv = furi_hal_power_get_battery_voltage_adc();
    return voltage_mv >= FURI_HAL_POWER_BATTERY_CHARGE_DONE_MV;
}

void furi_hal_power_shutdown(void) {
    furi_hal_power_insomnia_enter();

    furi_hal_bt_reinit();

    while(LL_HSEM_1StepLock(HSEM, CFG_HW_RCC_SEMID))
        ;

    if(!LL_HSEM_1StepLock(HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID)) {
        if(LL_PWR_IsActiveFlag_C2DS() || LL_PWR_IsActiveFlag_C2SB()) {
            // Release ENTRY_STOP_MODE semaphore
            LL_HSEM_ReleaseLock(HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID, 0);
        }
    }

    // Prepare Wakeup pin
    LL_PWR_SetWakeUpPinPolarityLow(LL_PWR_WAKEUP_PIN2);
    LL_PWR_EnableWakeUpPin(LL_PWR_WAKEUP_PIN2);
    LL_C2_PWR_EnableWakeUpPin(LL_PWR_WAKEUP_PIN2);

    /* Release RCC semaphore */
    LL_HSEM_ReleaseLock(HSEM, CFG_HW_RCC_SEMID, 0);

    LL_PWR_DisableBootC2();
    LL_PWR_SetPowerMode(LL_PWR_MODE_SHUTDOWN);
    LL_C2_PWR_SetPowerMode(LL_PWR_MODE_SHUTDOWN);
    LL_LPM_EnableDeepSleep();

    // FINAL unconditional wipe of ALL PWR pull-up/pull-down registers.
    // This MUST be the last thing before __WFI(). furi_hal_bt_reinit() above
    // restarts the BLE stack which re-adds PWR pulls via furi_hal_gpio_init().
    // Without this, BT-enabled shutdown leaks ~500-600µA through accumulated
    // pulls driving current into unpowered peripherals via ESD clamping diodes.
    // Interrupts disabled so nothing can sneak in between wipe and WFI.
    __disable_irq();
    WRITE_REG(PWR->PUCRA, 0);
    WRITE_REG(PWR->PDCRA, 0);
    WRITE_REG(PWR->PUCRB, 0);
    WRITE_REG(PWR->PDCRB, 0);
    WRITE_REG(PWR->PUCRC, 0);
    WRITE_REG(PWR->PDCRC, 0);
    WRITE_REG(PWR->PUCRD, 0);
    WRITE_REG(PWR->PDCRD, 0);
    WRITE_REG(PWR->PUCRE, 0);
    WRITE_REG(PWR->PDCRE, 0);
    WRITE_REG(PWR->PUCRH, 0);
    WRITE_REG(PWR->PDCRH, 0);
    // Only PA3 pull-down (keep periph_power OFF) and PC13 pull-up (OK button wakeup)
    LL_PWR_EnableGPIOPullDown(LL_PWR_GPIO_A, LL_PWR_GPIO_BIT_3);
    LL_PWR_EnableGPIOPullUp(LL_PWR_GPIO_C, LL_PWR_GPIO_BIT_13);

    __WFI();
    furi_crash("Insomniac core2");
}

void furi_hal_power_off(void) {
    // Properly shut down display and peripherals, then cut power
    furi_hal_power_disable_external_3_3v();

    // Enable RTC alarm output on PC13 for potential RTC wakeup.
    // RTC config is in the backup domain and survives system reset.
    furi_hal_rtc_prepare_for_shutdown();

    // Deferred shutdown: set flag in unused RTC backup register and reset.
    // After reset, furi_hal_init_early() detects this flag and enters SHUTDOWN
    // immediately — before CPU2 (BLE radio core) boots. This guarantees true
    // SHUTDOWN mode regardless of prior BLE state. Direct WFI-based shutdown
    // fails when CPU2 is actively running the BLE stack: the STM32WB power
    // controller requires BOTH CPUs in WFI/WFE to enter SHUTDOWN, and a
    // running CPU2 holds the system in Stop mode (~500-600uA) instead of
    // SHUTDOWN (~10uA). The system reset ensures CPU2 never starts (C2BOOT=0
    // after reset), so only CPU1 needs to WFI.
    LL_RTC_BAK_SetRegister(RTC, 19, 0xDEAD5077);
    NVIC_SystemReset();
    // Does not return — system resets and re-enters main()
}

FURI_NORETURN void furi_hal_power_reset(void) {
    NVIC_SystemReset();
}

bool furi_hal_power_enable_otg(void) {
    // No BQ25896 - OTG boost not available
    return false;
}

void furi_hal_power_disable_otg(void) {
    // No BQ25896 - OTG boost not available
}

bool furi_hal_power_is_otg_enabled(void) {
    // No BQ25896 - OTG boost not available
    return false;
}

float furi_hal_power_get_battery_charge_voltage_limit(void) {
    // No BQ25896 - return standard LiPo charge voltage
    return 4.2f;
}

void furi_hal_power_set_battery_charge_voltage_limit(float voltage) {
    // No BQ25896 - cannot control charge voltage
    UNUSED(voltage);
}

bool furi_hal_power_check_otg_fault(void) {
    // No BQ25896 - no OTG faults possible
    return false;
}

void furi_hal_power_check_otg_status(void) {
    // No BQ25896 - nothing to check
}

uint32_t furi_hal_power_get_battery_remaining_capacity(void) {
    // Estimate from voltage-based percentage
    uint8_t pct = furi_hal_power_get_pct();
    return (uint32_t)((FURI_HAL_POWER_BATTERY_DESIGN_CAPACITY * pct) / 100);
}

uint32_t furi_hal_power_get_battery_full_capacity(void) {
    // No fuel gauge to track learned capacity - return design capacity
    return FURI_HAL_POWER_BATTERY_DESIGN_CAPACITY;
}

uint32_t furi_hal_power_get_battery_design_capacity(void) {
    return FURI_HAL_POWER_BATTERY_DESIGN_CAPACITY;
}

float furi_hal_power_get_battery_voltage(FuriHalPowerIC ic) {
    UNUSED(ic);
    // Both IC selections return the same ADC-based voltage reading
    uint16_t voltage_mv = furi_hal_power_get_battery_voltage_adc();
    return (float)voltage_mv / 1000.0f;
}

float furi_hal_power_get_battery_current(FuriHalPowerIC ic) {
    UNUSED(ic);
    // No current measurement available - return dummy 20mA
    return 0.020f;
}

static float furi_hal_power_get_battery_temperature_internal(FuriHalPowerIC ic) {
    UNUSED(ic);
    // No temperature sensor available - return room temperature
    return 25.0f;
}

float furi_hal_power_get_battery_temperature(FuriHalPowerIC ic) {
    return furi_hal_power_get_battery_temperature_internal(ic);
}

float furi_hal_power_get_usb_voltage(void) {
    // Return 5.0V when USB is connected, 0.0V otherwise
    if(furi_hal_usb_is_connected()) return 5.0f;
    return 0.0f;
}

// send bytes to prevent potential paracitic power 
static void furi_hal_power_spi2_tx_raw(const uint8_t* data, size_t size) {
    SPI_TypeDef* spi = SPI2;
    for(size_t i = 0; i < size; i++) {
        while(!LL_SPI_IsActiveFlag_TXE(spi))
            ;
        LL_SPI_TransmitData8(spi, data[i]);
    }
    // Wait for TX complete and drain RX FIFO
    while(LL_SPI_GetTxFIFOLevel(spi) != LL_SPI_TX_FIFO_EMPTY)
        ;
    while(LL_SPI_IsActiveFlag_BSY(spi))
        ;
    while(LL_SPI_GetRxFIFOLevel(spi) != LL_SPI_RX_FIFO_EMPTY) {
        LL_SPI_ReceiveData8(spi);
    }
}

void furi_hal_power_enable_external_3_3v(void) {
    // Enable the peripheral 3V3 rail
    furi_hal_gpio_write(&gpio_periph_power, 1);

    // Let 3V3 rail stabilize
    furi_delay_ms(5);

    // restore pins
    furi_hal_gpio_init_ex(
        furi_hal_spi_bus_handle_display.mosi,
        GpioModeAltFunctionPushPull,
        GpioPullNo,
        GpioSpeedVeryHigh,
        GpioAltFn5SPI2);
    furi_hal_gpio_init_ex(
        furi_hal_spi_bus_handle_display.sck,
        GpioModeAltFunctionPushPull,
        GpioPullNo,
        GpioSpeedVeryHigh,
        GpioAltFn5SPI2);
    furi_hal_gpio_init_ex(
        furi_hal_spi_bus_handle_display.miso,
        GpioModeAltFunctionPushPull,
        GpioPullNo,
        GpioSpeedVeryHigh,
        GpioAltFn5SPI2);

    // Restore display CS
    furi_hal_gpio_write(furi_hal_spi_bus_handle_display.cs, true);
    furi_hal_gpio_init(
        furi_hal_spi_bus_handle_display.cs,
        GpioModeOutputPushPull,
        GpioPullUp,
        GpioSpeedVeryHigh);

    // Restore SD card CS
    furi_hal_gpio_write(&gpio_sdcard_cs, true);
    furi_hal_gpio_init(
        &gpio_sdcard_cs,
        GpioModeOutputPushPull,
        GpioPullUp,
        GpioSpeedVeryHigh);

    // Restore SD card detect pin
    furi_hal_gpio_init(&gpio_sdcard_cd, GpioModeInput, GpioPullUp, GpioSpeedLow);

    // Release display RST
    furi_hal_gpio_write(&gpio_display_rst_n, 1);

    // Let screen stabilize
    furi_delay_ms(10);

    // Reinit the screen 
    {
        bool bus_was_acquired = (furi_hal_spi_bus_d.current_handle != NULL);

        if(!bus_was_acquired) {
            furi_hal_spi_acquire(&furi_hal_spi_bus_handle_display);
        } else {
            furi_hal_gpio_write(furi_hal_spi_bus_handle_display.cs, false);
        }

        // D/C low = command mode
        furi_hal_gpio_write(&gpio_display_di, 0);

        // full init sequence
        uint8_t contrast = (furi_hal_version_get_hw_display() == FuriHalVersionDisplayMgg) ?
                               112 : 128;
        uint8_t init_cmds[] = {
            0xAE,       // Display OFF
            0x20, 0x02, // Set Memory Addressing Mode → Page
            0xD5, 0x80, // Set clock divide ratio
            0xA8, 0x3F, // Set MUX ratio (64 lines)
            0xD3, 0x00, // Set display offset
            0x40,       // Set start line 0
            0x8D, 0x14, // Enable charge pump
            0xA1,       // Segment remap ON
            0xC8,       // COM scan direction DEC
            0xDA, 0x12, // COM pins config
            0x81, contrast, // Set contrast
            0xD9, 0xF1, // Set precharge period
            0xDB, 0x40, // Set VCOMH deselect level
            0xA4,       // Resume from entire display on
            0xA6,       // Normal display (not inverted)
            0xAF,       // Display ON
        };

        if(!bus_was_acquired) {
            furi_hal_spi_bus_tx(
                &furi_hal_spi_bus_handle_display, init_cmds, sizeof(init_cmds), 1000);
            furi_hal_spi_release(&furi_hal_spi_bus_handle_display);
        } else {
            furi_hal_power_spi2_tx_raw(init_cmds, sizeof(init_cmds));
            furi_hal_gpio_write(furi_hal_spi_bus_handle_display.cs, true);
        }
    }
}

void furi_hal_power_disable_external_3_3v(void) {
    // SSD1306 parasitic power prevention:
    bool bus_was_acquired = (furi_hal_spi_bus_d.current_handle != NULL);

    if(!bus_was_acquired) {
        furi_hal_spi_acquire(&furi_hal_spi_bus_handle_display);
    } else {

        furi_hal_gpio_write(furi_hal_spi_bus_handle_display.cs, false);
    }

    furi_hal_gpio_write(&gpio_display_di, 0);

    if(!bus_was_acquired) {
        // Display OFF
        uint8_t cmd_display_off = 0xAE;
        furi_hal_spi_bus_tx(
            &furi_hal_spi_bus_handle_display, &cmd_display_off, 1, 100);

        // Disable charge pump
        uint8_t cmd_charge_pump[2] = {0x8D, 0x10};
        furi_hal_spi_bus_tx(
            &furi_hal_spi_bus_handle_display, cmd_charge_pump, 2, 100);

        // Release the bus normally
        furi_hal_spi_release(&furi_hal_spi_bus_handle_display);
    } else {
        uint8_t cmds[3] = {0xAE, 0x8D, 0x10};
        furi_hal_power_spi2_tx_raw(cmds, sizeof(cmds));

        furi_hal_gpio_write(furi_hal_spi_bus_handle_display.cs, true);
    }

    furi_delay_ms(20);

    // set all pins low to prevent parasitic power
    furi_hal_gpio_init_ex(
        furi_hal_spi_bus_handle_display.mosi,
        GpioModeOutputPushPull,
        GpioPullNo,
        GpioSpeedLow,
        GpioAltFnUnused);
    furi_hal_gpio_write(furi_hal_spi_bus_handle_display.mosi, false);

    furi_hal_gpio_init_ex(
        furi_hal_spi_bus_handle_display.sck,
        GpioModeOutputPushPull,
        GpioPullNo,
        GpioSpeedLow,
        GpioAltFnUnused);
    furi_hal_gpio_write(furi_hal_spi_bus_handle_display.sck, false);

    furi_hal_gpio_init_ex(
        furi_hal_spi_bus_handle_display.miso,
        GpioModeOutputPushPull,
        GpioPullNo,
        GpioSpeedLow,
        GpioAltFnUnused);
    furi_hal_gpio_write(furi_hal_spi_bus_handle_display.miso, false);

    furi_hal_gpio_write(furi_hal_spi_bus_handle_display.cs, false);
    furi_hal_gpio_write(&gpio_sdcard_cs, false);

    furi_hal_gpio_write(&gpio_display_di, false);

    furi_hal_gpio_init_simple(&gpio_sdcard_cd, GpioModeOutputOpenDrain);
    furi_hal_gpio_write(&gpio_sdcard_cd, false);

    furi_hal_gpio_write(&gpio_display_rst_n, false);

    furi_hal_gpio_write(&gpio_periph_power, 0);
}

void furi_hal_power_suppress_charge_enter(void) {
    FURI_CRITICAL_ENTER();
    furi_hal_power.suppress_charge++;
    FURI_CRITICAL_EXIT();
    // No BQ25896 - cannot control charging
}

void furi_hal_power_suppress_charge_exit(void) {
    FURI_CRITICAL_ENTER();
    furi_hal_power.suppress_charge--;
    FURI_CRITICAL_EXIT();
    // No BQ25896 - cannot control charging
}

void furi_hal_power_info_get(PropertyValueCallback out, char sep, void* context) {
    furi_check(out);

    FuriString* value = furi_string_alloc();
    FuriString* key = furi_string_alloc();

    PropertyValueContext property_context = {
        .key = key, .value = value, .out = out, .sep = sep, .last = false, .context = context};

    if(sep == '.') {
        property_value_out(&property_context, NULL, 2, "format", "major", "2");
        property_value_out(&property_context, NULL, 2, "format", "minor", "1");
    } else {
        property_value_out(&property_context, NULL, 3, "power", "info", "major", "2");
        property_value_out(&property_context, NULL, 3, "power", "info", "minor", "1");
    }

    uint8_t charge = furi_hal_power_get_pct();
    property_value_out(&property_context, "%u", 2, "charge", "level", charge);

    const char* charge_state;
    if(furi_hal_power_is_charging()) {
        if((charge < 100) && (!furi_hal_power_is_charging_done())) {
            charge_state = "charging";
        } else {
            charge_state = "charged";
        }
    } else {
        charge_state = "discharging";
    }

    property_value_out(&property_context, NULL, 2, "charge", "state", charge_state);
    uint16_t charge_voltage_limit =
        (uint16_t)(furi_hal_power_get_battery_charge_voltage_limit() * 1000.f);
    property_value_out(
        &property_context, "%u", 3, "charge", "voltage", "limit", charge_voltage_limit);
    uint16_t voltage =
        (uint16_t)(furi_hal_power_get_battery_voltage(FuriHalPowerICFuelGauge) * 1000.f);
    property_value_out(&property_context, "%u", 2, "battery", "voltage", voltage);
    int16_t current =
        (int16_t)(furi_hal_power_get_battery_current(FuriHalPowerICFuelGauge) * 1000.f);
    property_value_out(&property_context, "%d", 2, "battery", "current", current);
    int16_t temperature = (int16_t)furi_hal_power_get_battery_temperature(FuriHalPowerICFuelGauge);
    property_value_out(&property_context, "%d", 2, "battery", "temp", temperature);
    property_value_out(
        &property_context, "%u", 2, "battery", "health", furi_hal_power_get_bat_health_pct());
    property_value_out(
        &property_context,
        "%lu",
        2,
        "capacity",
        "remain",
        furi_hal_power_get_battery_remaining_capacity());
    property_value_out(
        &property_context,
        "%lu",
        2,
        "capacity",
        "full",
        furi_hal_power_get_battery_full_capacity());
    property_context.last = true;
    property_value_out(
        &property_context,
        "%lu",
        2,
        "capacity",
        "design",
        furi_hal_power_get_battery_design_capacity());

    furi_string_free(key);
    furi_string_free(value);
}

void furi_hal_power_debug_get(PropertyValueCallback out, void* context) {
    furi_check(out);

    FuriString* value = furi_string_alloc();
    FuriString* key = furi_string_alloc();

    PropertyValueContext property_context = {
        .key = key, .value = value, .out = out, .sep = '.', .last = false, .context = context};

    // Power Debug version
    property_value_out(&property_context, NULL, 2, "format", "major", "2");
    property_value_out(&property_context, NULL, 2, "format", "minor", "0");

    // ADC-based battery readings
    uint16_t battery_mv = furi_hal_power_get_battery_voltage_adc();
    property_value_out(&property_context, "%d", 2, "adc", "battery_mv", (int)battery_mv);
    property_value_out(
        &property_context,
        "%d",
        2,
        "adc",
        "battery_pct",
        (int)furi_hal_power_voltage_to_pct(battery_mv));

    // USB connection state
    bool usb_connected = furi_hal_usb_is_connected();
    property_value_out(&property_context, "%d", 2, "usb", "connected", (int)usb_connected);
    property_value_out(
        &property_context,
        "%d",
        2,
        "usb",
        "vbus_mv",
        usb_connected ? 5000 : 0);

    // Charging state
    property_value_out(
        &property_context, "%d", 2, "charge", "active", (int)furi_hal_power_is_charging());

    property_context.last = true;
    property_value_out(
        &property_context,
        "%d",
        2,
        "charge",
        "done",
        (int)furi_hal_power_is_charging_done());

    furi_string_free(key);
    furi_string_free(value);
}
