#include "u8g2_glue.h"

#include <furi_hal.h>

// SSD1306/SSD1315 contrast range is 0-255
#define CONTRAST_ERC 255
#define CONTRAST_MGG 255

/* SSD1306/SSD1315 Command Definitions */
#define SSD1306_CMD_DISPLAY_OFF        0xAE
#define SSD1306_CMD_DISPLAY_ON         0xAF
#define SSD1306_CMD_SET_CONTRAST       0x81
#define SSD1306_CMD_ENTIRE_DISPLAY_OFF 0xA4
#define SSD1306_CMD_ENTIRE_DISPLAY_ON  0xA5
#define SSD1306_CMD_NORMAL_DISPLAY     0xA6
#define SSD1306_CMD_INVERT_DISPLAY     0xA7
#define SSD1306_CMD_SET_MUX_RATIO      0xA8
#define SSD1306_CMD_SET_DISPLAY_OFFSET 0xD3
#define SSD1306_CMD_SET_START_LINE     0x40
#define SSD1306_CMD_SET_SEG_REMAP_OFF  0xA0
#define SSD1306_CMD_SET_SEG_REMAP_ON   0xA1
#define SSD1306_CMD_SET_COM_SCAN_INC   0xC0
#define SSD1306_CMD_SET_COM_SCAN_DEC   0xC8
#define SSD1306_CMD_SET_COM_PINS       0xDA
#define SSD1306_CMD_SET_CLK_DIV        0xD5
#define SSD1306_CMD_SET_PRECHARGE      0xD9
#define SSD1306_CMD_SET_VCOMH          0xDB
#define SSD1306_CMD_CHARGE_PUMP        0x8D
#define SSD1306_CMD_SET_PAGE_ADDR      0xB0
#define SSD1306_CMD_SET_LOW_COLUMN     0x00
#define SSD1306_CMD_SET_HIGH_COLUMN    0x10
#define SSD1306_CMD_SET_MEM_ADDR_MODE  0x20

#define SSD1306_BRIGHTNESS_STEPS    32
#define SSD1306_BACKLIGHT_MIN        13  /* 5% of 255, absolute floor */
#define SSD1306_CONTRAST_FLOOR       5   /* minimum contrast that stays visible */
#define SSD1306_PRECHARGE_PH2_MIN    2   /* minimum phase-2 that stays visible */

typedef struct {
    uint8_t contrast;
    uint8_t precharge;
    uint8_t vcomh;
} SSD1306BrightnessParams;

static bool ssd1306_display_on = 1;
static SSD1306BrightnessParams ssd1306_current_params = {CONTRAST_ERC, 0xF1, 0x40};
static uint8_t ssd1306_backlight_level = 0xFF;
static int8_t ssd1306_contrast_offset = 0;

/*
 * Map a backlight level (1-255) to SSD1306 register values.
 * Uses three registers for a wide dimming range:
 *   - Contrast (0x81):   pixel drive current
 *   - Pre-charge (0xD9): shorter phase-2 = less charge = dimmer
 *   - VCOMH (0xDB):      lower deselect voltage = smaller swing = dimmer
 * Quadratic gamma on contrast for perceptual linearity.
 */
static SSD1306BrightnessParams u8x8_d_st756x_backlight_to_params(uint8_t backlight_level) {
    SSD1306BrightnessParams params;

    /* Clamp to floor so display never fully turns off */
    if(backlight_level < SSD1306_BACKLIGHT_MIN) {
        backlight_level = SSD1306_BACKLIGHT_MIN;
    }

    /* Scale input SSD1306_BACKLIGHT_MIN-255 to 0-1000 for finer math */
    uint16_t range = 255 - SSD1306_BACKLIGHT_MIN;
    uint16_t bl = (uint16_t)(backlight_level - SSD1306_BACKLIGHT_MIN) * 1000 / range;

    /* VCOMH deselect level — lower = dimmer */
    if(bl <= 250) {
        params.vcomh = 0x00;      /* ~0.65 * Vcc — dimmest */
    } else if(bl <= 500) {
        params.vcomh = 0x20;      /* ~0.77 * Vcc */
    } else {
        params.vcomh = 0x40;      /* ~1.0  * Vcc — brightest */
    }

    /* Pre-charge: phase-2 scales SSD1306_PRECHARGE_PH2_MIN..15 across range, phase-1 fixed at 1 */
    uint8_t ph2 = SSD1306_PRECHARGE_PH2_MIN +
                  (uint8_t)((uint32_t)bl * (15 - SSD1306_PRECHARGE_PH2_MIN) / 1000);
    params.precharge = (ph2 << 4) | 0x01;

    /* Contrast: LINEAR mapping SSD1306_CONTRAST_FLOOR-255.
     * Floor ensures 5% is still visibly on.
     * 100% backlight = contrast 255 (absolute SSD1306 max) */
    int16_t c = (int16_t)(SSD1306_CONTRAST_FLOOR +
                          (uint32_t)bl * (255 - SSD1306_CONTRAST_FLOOR) / 1000UL) +
                (int16_t)ssd1306_contrast_offset * 4;
    if(c < SSD1306_CONTRAST_FLOOR) c = SSD1306_CONTRAST_FLOOR;
    if(c > 255) c = 255;
    params.contrast = (uint8_t)c;

    return params;
}

/*
 * Smooth transition from current register state to target.
 * Contrast is linearly interpolated over up to BRIGHTNESS_STEPS steps.
 * VCOMH / pre-charge switch order depends on direction:
 *   dimming  → reduce VCOMH+precharge first, then ramp contrast down
 *   brighten → ramp contrast up first, then raise VCOMH+precharge
 * This avoids visible glitches at register-change boundaries.
 */
static void u8x8_d_st756x_apply_brightness_smooth(
    u8x8_t* u8x8,
    const SSD1306BrightnessParams* target) {
    SSD1306BrightnessParams start = ssd1306_current_params;

    if(start.contrast == target->contrast &&
       start.precharge == target->precharge &&
       start.vcomh == target->vcomh) {
        return;
    }

    int16_t c_diff = (int16_t)target->contrast - (int16_t)start.contrast;
    uint16_t abs_diff = (c_diff >= 0) ? (uint16_t)c_diff : (uint16_t)(-c_diff);
    uint8_t steps = (abs_diff > SSD1306_BRIGHTNESS_STEPS) ?
                        SSD1306_BRIGHTNESS_STEPS :
                        (abs_diff > 0 ? (uint8_t)abs_diff : 1);

    bool dimming = (target->contrast < start.contrast);
    bool regs_changed = (start.precharge != target->precharge ||
                         start.vcomh != target->vcomh);

    /* Dimming: drop VCOMH / pre-charge first */
    if(dimming && regs_changed) {
        u8x8_cad_StartTransfer(u8x8);
        u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_VCOMH);
        u8x8_cad_SendArg(u8x8, target->vcomh);
        u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_PRECHARGE);
        u8x8_cad_SendArg(u8x8, target->precharge);
        u8x8_cad_EndTransfer(u8x8);
        ssd1306_current_params.vcomh = target->vcomh;
        ssd1306_current_params.precharge = target->precharge;
    }

    /* Ramp contrast */
    for(uint8_t i = 1; i <= steps; i++) {
        uint8_t c = (uint8_t)(
            (int16_t)start.contrast + (c_diff * (int16_t)i) / (int16_t)steps);
        u8x8_cad_StartTransfer(u8x8);
        u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_CONTRAST);
        u8x8_cad_SendArg(u8x8, c);
        u8x8_cad_EndTransfer(u8x8);
        ssd1306_current_params.contrast = c;
        if(i < steps) {
            furi_delay_ms(8);
        }
    }

    /* Brightening: raise VCOMH / pre-charge after contrast ramp */
    if(!dimming && regs_changed) {
        u8x8_cad_StartTransfer(u8x8);
        u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_PRECHARGE);
        u8x8_cad_SendArg(u8x8, target->precharge);
        u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_VCOMH);
        u8x8_cad_SendArg(u8x8, target->vcomh);
        u8x8_cad_EndTransfer(u8x8);
    }

    ssd1306_current_params = *target;
}

uint8_t u8g2_gpio_and_delay_stm32(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
    UNUSED(u8x8);
    UNUSED(arg_ptr);
    switch(msg) {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        /* HAL initialization contains all what we need so we can skip this part. */
        break;
    case U8X8_MSG_DELAY_MILLI:
        furi_delay_ms(arg_int);
        break;
    case U8X8_MSG_DELAY_10MICRO:
        furi_delay_us(10);
        break;
    case U8X8_MSG_DELAY_100NANO:
        asm("nop");
        break;
    case U8X8_MSG_GPIO_RESET:
        furi_hal_gpio_write(&gpio_display_rst_n, arg_int);
        break;
    default:
        return 0;
    }

    return 1;
}

uint8_t u8x8_hw_spi_stm32(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
    UNUSED(u8x8);
    switch(msg) {
    case U8X8_MSG_BYTE_SEND:
        furi_hal_spi_bus_tx(&furi_hal_spi_bus_handle_display, (uint8_t*)arg_ptr, arg_int, 10000);
        break;
    case U8X8_MSG_BYTE_SET_DC:
        furi_hal_gpio_write(&gpio_display_di, arg_int);
        break;
    case U8X8_MSG_BYTE_INIT:
        break;
    case U8X8_MSG_BYTE_START_TRANSFER:
        furi_hal_spi_acquire(&furi_hal_spi_bus_handle_display);
        break;
    case U8X8_MSG_BYTE_END_TRANSFER:
        furi_hal_spi_release(&furi_hal_spi_bus_handle_display);
        break;
    default:
        return 0;
    }

    return 1;
}

static const uint8_t u8x8_d_st756x_powersave0_seq[] = {
    U8X8_START_TRANSFER(), /* enable chip, delay is part of the transfer start */
    U8X8_C(SSD1306_CMD_ENTIRE_DISPLAY_OFF), /* resume from entire display on */
    U8X8_C(SSD1306_CMD_DISPLAY_ON), /* display on */
    U8X8_END_TRANSFER(), /* disable chip */
    U8X8_END() /* end of sequence */
};

static const uint8_t u8x8_d_st756x_powersave1_seq[] = {
    U8X8_START_TRANSFER(), /* enable chip, delay is part of the transfer start */
    U8X8_C(SSD1306_CMD_DISPLAY_OFF), /* display off */
    U8X8_C(SSD1306_CMD_ENTIRE_DISPLAY_ON), /* entire display on (power save) */
    U8X8_END_TRANSFER(), /* disable chip */
    U8X8_END() /* end of sequence */
};

// Flip sequences - SSD1306 uses same commands as ST756X for segment/COM remap
static const uint8_t u8x8_d_st756x_flip0_seq[] = {
    U8X8_START_TRANSFER(), /* enable chip, delay is part of the transfer start */
    U8X8_C(SSD1306_CMD_SET_SEG_REMAP_OFF), /* segment remap a0/a1*/
    U8X8_C(SSD1306_CMD_SET_COM_SCAN_INC), /* c0: scan dir normal, c8: reverse */
    U8X8_END_TRANSFER(), /* disable chip */
    U8X8_END() /* end of sequence */
};

static const uint8_t u8x8_d_st756x_flip1_seq[] = {
    U8X8_START_TRANSFER(), /* enable chip, delay is part of the transfer start */
    U8X8_C(SSD1306_CMD_SET_SEG_REMAP_ON), /* segment remap a0/a1*/
    U8X8_C(SSD1306_CMD_SET_COM_SCAN_DEC), /* c0: scan dir normal, c8: reverse */
    U8X8_END_TRANSFER(), /* disable chip */
    U8X8_END() /* end of sequence */
};

static const u8x8_display_info_t u8x8_st756x_128x64_display_info = {
    .chip_enable_level = 0,
    .chip_disable_level = 1,
    .post_chip_enable_wait_ns = 150,
    .pre_chip_disable_wait_ns = 50,
    .reset_pulse_width_ms = 10, /* SSD1306 needs stable reset, increased from 1ms */
    .post_reset_wait_ms = 100,  /* SSD1306 needs 100ms after reset before commands */
    .sda_setup_time_ns = 50,
    .sck_pulse_width_ns = 120,
    .sck_clock_hz = 4000000UL,
    .spi_mode = 0, /* active high, rising edge */
    .i2c_bus_clock_100kHz = 4,
    .data_setup_time_ns = 40,
    .write_pulse_width_ns = 80,
    .tile_width = 16, /* width of 16*8=128 pixel */
    .tile_height = 8,
    .default_x_offset = 0,
    .flipmode_x_offset = 0, /* SSD1306 has no column offset */
    .pixel_width = 128,
    .pixel_height = 64};

uint8_t u8x8_d_st756x_common(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
    uint8_t x, c;
    uint8_t* ptr;

    switch(msg) {
    case U8X8_MSG_DISPLAY_DRAW_TILE:
        u8x8_cad_StartTransfer(u8x8);

        x = ((u8x8_tile_t*)arg_ptr)->x_pos;
        x *= 8;
        x += u8x8->x_offset;
        u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_HIGH_COLUMN | (x >> 4));
        u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_LOW_COLUMN | (x & 15));
        u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_PAGE_ADDR | (((u8x8_tile_t*)arg_ptr)->y_pos));

        c = ((u8x8_tile_t*)arg_ptr)->cnt;
        c *= 8;
        ptr = ((u8x8_tile_t*)arg_ptr)->tile_ptr;
        /* 
                The following if condition checks the hardware limits of the st7565 
                controller: It is not allowed to write beyond the display limits.
                This is in fact an issue within flip mode.
            */
        if(c + x > 128u) {
            c = 128u;
            c -= x;
        }

        do {
            u8x8_cad_SendData(
                u8x8, c, ptr); /* note: SendData can not handle more than 255 bytes */
            arg_int--;
        } while(arg_int > 0);

        u8x8_cad_EndTransfer(u8x8);
        break;
    case U8X8_MSG_DISPLAY_SET_POWER_SAVE:
        if(arg_int == 0)
            u8x8_cad_SendSequence(u8x8, u8x8_d_st756x_powersave0_seq);
        else
            u8x8_cad_SendSequence(u8x8, u8x8_d_st756x_powersave1_seq);
        break;
#ifdef U8X8_WITH_SET_CONTRAST
    case U8X8_MSG_DISPLAY_SET_CONTRAST:
        u8x8_cad_StartTransfer(u8x8);
        u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_CONTRAST);
        u8x8_cad_SendArg(u8x8, arg_int); /* SSD1306 has full 0-255 range */
        u8x8_cad_EndTransfer(u8x8);
        break;
#endif
    default:
        return 0;
    }
    return 1;
}

void u8x8_d_st756x_init(u8x8_t* u8x8, uint8_t contrast, uint8_t regulation_ratio, bool bias) {
    UNUSED(regulation_ratio);
    UNUSED(bias);

    u8x8_cad_StartTransfer(u8x8);

    // Display off during init
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_DISPLAY_OFF);

    // Set Memory Addressing Mode to Page Addressing (0x02)
    // This is critical - SSD1306 defaults to horizontal mode!
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_MEM_ADDR_MODE);
    u8x8_cad_SendArg(u8x8, 0x02);

    // Set clock divide ratio and oscillator frequency
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_CLK_DIV);
    u8x8_cad_SendArg(u8x8, 0x80);

    // Set multiplex ratio (64 lines)
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_MUX_RATIO);
    u8x8_cad_SendArg(u8x8, 0x3F);

    // Set display offset
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_DISPLAY_OFFSET);
    u8x8_cad_SendArg(u8x8, 0x00);

    // Set start line
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_START_LINE | 0x00);

    // Enable charge pump
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_CHARGE_PUMP);
    u8x8_cad_SendArg(u8x8, 0x14); // Enable charge pump

    // Set segment remap and COM scan direction
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_SEG_REMAP_ON);
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_COM_SCAN_DEC);

    // Set COM pins hardware configuration
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_COM_PINS);
    u8x8_cad_SendArg(u8x8, 0x12);

    // Set contrast
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_CONTRAST);
    u8x8_cad_SendArg(u8x8, contrast);

    // Set precharge period
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_PRECHARGE);
    u8x8_cad_SendArg(u8x8, 0xF1);

    // Set VCOMH deselect level
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_VCOMH);
    u8x8_cad_SendArg(u8x8, 0x40);

    // Resume from entire display on
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_ENTIRE_DISPLAY_OFF);

    // Set normal display (not inverted)
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_NORMAL_DISPLAY);

    // Display on
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_DISPLAY_ON);

    u8x8_cad_EndTransfer(u8x8);

    ssd1306_display_on = 1;
    ssd1306_current_params.contrast = contrast;
    ssd1306_current_params.precharge = 0xF1;
    ssd1306_current_params.vcomh = 0x40;
    ssd1306_backlight_level = 0xFF;
}

void u8x8_d_st756x_set_contrast(u8x8_t* u8x8, int8_t contrast_offset) {
    ssd1306_contrast_offset = contrast_offset;

    if(!ssd1306_display_on) {
        return;
    }

    SSD1306BrightnessParams target =
        u8x8_d_st756x_backlight_to_params(ssd1306_backlight_level);
    u8x8_d_st756x_apply_brightness_smooth(u8x8, &target);
}

void u8x8_d_st756x_set_brightness(u8x8_t* u8x8, uint8_t backlight_level, bool display_on) {
    UNUSED(display_on);

    if(backlight_level < SSD1306_BACKLIGHT_MIN) {
        backlight_level = SSD1306_BACKLIGHT_MIN;
    }

    if(!ssd1306_display_on) {
        u8x8_cad_StartTransfer(u8x8);
        u8x8_cad_SendCmd(u8x8, SSD1306_CMD_DISPLAY_ON);
        u8x8_cad_EndTransfer(u8x8);
        ssd1306_display_on = 1;
    }

    ssd1306_backlight_level = backlight_level;
    SSD1306BrightnessParams target =
        u8x8_d_st756x_backlight_to_params(backlight_level);
    u8x8_d_st756x_apply_brightness_smooth(u8x8, &target);
}

uint8_t u8x8_d_st756x_flipper(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
    /* call common procedure first and handle messages there */
    if(u8x8_d_st756x_common(u8x8, msg, arg_int, arg_ptr) == 0) {
        /* msg not handled, then try here */
        switch(msg) {
        case U8X8_MSG_DISPLAY_SETUP_MEMORY:
            u8x8_d_helper_display_setup_memory(u8x8, &u8x8_st756x_128x64_display_info);
            break;
        case U8X8_MSG_DISPLAY_INIT:
            u8x8_d_helper_display_init(u8x8);
            FuriHalVersionDisplay display = furi_hal_version_get_hw_display();
            if(display == FuriHalVersionDisplayMgg) {
                u8x8_d_st756x_init(u8x8, CONTRAST_MGG, 0, false);
            } else {
                u8x8_d_st756x_init(u8x8, CONTRAST_ERC, 0, false);
            }
            break;
        case U8X8_MSG_DISPLAY_SET_FLIP_MODE:
            if(arg_int == 0) {
                u8x8_cad_SendSequence(u8x8, u8x8_d_st756x_flip1_seq);
                u8x8->x_offset = u8x8->display_info->default_x_offset;
            } else {
                u8x8_cad_SendSequence(u8x8, u8x8_d_st756x_flip0_seq);
                u8x8->x_offset = u8x8->display_info->flipmode_x_offset;
            }
            break;
        default:
            /* msg unknown */
            return 0;
        }
    }
    return 1;
}

void u8g2_Setup_st756x_flipper(
    u8g2_t* u8g2,
    const u8g2_cb_t* rotation,
    u8x8_msg_cb byte_cb,
    u8x8_msg_cb gpio_and_delay_cb) {
    uint8_t tile_buf_height;
    uint8_t* buf;
    u8g2_SetupDisplay(u8g2, u8x8_d_st756x_flipper, u8x8_cad_001, byte_cb, gpio_and_delay_cb);
    buf = u8g2_m_16_8_f(&tile_buf_height);
    u8g2_SetupBuffer(u8g2, buf, tile_buf_height, u8g2_ll_hvline_vertical_top_lsb, rotation);
}
