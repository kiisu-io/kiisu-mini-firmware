#include <furi_hal.h>
#include <furi_hal_mpu.h>
#include <furi_hal_memory.h>

#include <stm32wbxx_ll_cortex.h>
#include <stm32wbxx_ll_pwr.h>
#include <stm32wbxx_ll_rtc.h>

#define TAG "FuriHal"

void furi_hal_init_early(void) {
    furi_hal_cortex_init_early();
    furi_hal_clock_init_early();
    furi_hal_bus_init_early();
    furi_hal_dma_init_early();
    furi_hal_resources_init_early();
    furi_hal_os_init();
    furi_hal_spi_config_init_early();
    furi_hal_i2c_init_early();
    furi_hal_light_init();
    furi_hal_rtc_init_early();


    if(LL_RTC_BAK_GetRegister(RTC, 19) == 0xDEAD5077) {
        LL_RTC_BAK_SetRegister(RTC, 19, 0); // Clear flag so next boot is normal

        __disable_irq();

        // Why not
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

        // PA3 pull-down: keep periph_power OFF
        LL_PWR_EnableGPIOPullDown(LL_PWR_GPIO_A, LL_PWR_GPIO_BIT_3);
        // PC13 pull-up: OK button / WAKEUP_PIN2 needs defined state
        LL_PWR_EnableGPIOPullUp(LL_PWR_GPIO_C, LL_PWR_GPIO_BIT_13);

        // Configure wakeup on OK button
        LL_PWR_SetWakeUpPinPolarityLow(LL_PWR_WAKEUP_PIN2);
        LL_PWR_EnableWakeUpPin(LL_PWR_WAKEUP_PIN2);

        // Enter SHUTDOWN
        LL_PWR_SetPowerMode(LL_PWR_MODE_SHUTDOWN);
        LL_C2_PWR_SetPowerMode(LL_PWR_MODE_SHUTDOWN);
        LL_LPM_EnableDeepSleep();

        __WFI();
        // Wakeup from SHUTDOWN = POR, execution never reaches here
        NVIC_SystemReset();
    }

    furi_hal_version_init();
}

void furi_hal_deinit_early(void) {
    furi_hal_rtc_deinit_early();
    furi_hal_i2c_deinit_early();
    furi_hal_spi_config_deinit_early();
    furi_hal_resources_deinit_early();
    furi_hal_dma_deinit_early();
    furi_hal_bus_deinit_early();
    furi_hal_clock_deinit_early();
}

void furi_hal_init(void) {
    furi_hal_mpu_init();
    furi_hal_adc_init();
    furi_hal_clock_init();
    furi_hal_random_init();
    furi_hal_serial_control_init();
    furi_hal_rtc_init();
    furi_hal_interrupt_init();
    furi_hal_flash_init();
    furi_hal_resources_init();
    furi_hal_region_init();
    furi_hal_spi_config_init();
    furi_hal_spi_dma_init();
    furi_hal_ibutton_init();
    furi_hal_speaker_init();
    furi_hal_crypto_init();
    furi_hal_i2c_init();
    furi_hal_power_init();
    furi_hal_light_init();
    furi_hal_bt_init();
    furi_hal_memory_init();

#ifndef FURI_RAM_EXEC
    furi_hal_usb_init();
    furi_hal_vibro_init();
    furi_hal_subghz_init();
    furi_hal_nfc_init();
    furi_hal_rfid_init();
#endif
}

void furi_hal_switch(void* address) {
    __set_BASEPRI(0);
    // This code emulates system reset: sets MSP and calls Reset ISR
    asm volatile("ldr    r3, [%0]    \n" // Load SP from new vector to r3
                 "msr    msp, r3     \n" // Set MSP from r3
                 "ldr    r3, [%1]    \n" // Load Reset Handler address to r3
                 "mov    pc, r3      \n" // Set PC from r3 (jump to Reset ISR)
                 :
                 : "r"(address), "r"(address + 0x4)
                 : "r3");
}
