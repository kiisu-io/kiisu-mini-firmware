#include "u8g2_glue.h"

#include <furi_hal.h>

// TODO: SSD1306/1315 contrast range is 0-255, tune these values
#define CONTRAST_ERC 128
#define CONTRAST_MGG 112

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

// TODO: Verify power save sequences work correctly on SSD1306/1315
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

// TODO: Verify timing values for SSD1306/1315, current values from ST7565
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
    u8x8_cad_SendArg(u8x8, 0x80); // TODO: Tune clock if needed

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
    u8x8_cad_SendArg(u8x8, 0x12); // TODO: May need 0x02 for some displays

    // Set contrast
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_CONTRAST);
    u8x8_cad_SendArg(u8x8, contrast);

    // Set precharge period
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_PRECHARGE);
    u8x8_cad_SendArg(u8x8, 0xF1); // TODO: Tune precharge if needed

    // Set VCOMH deselect level
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_VCOMH);
    u8x8_cad_SendArg(u8x8, 0x40); // TODO: Tune VCOMH if needed

    // Resume from entire display on
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_ENTIRE_DISPLAY_OFF);

    // Set normal display (not inverted)
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_NORMAL_DISPLAY);

    // Display on
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_DISPLAY_ON);

    u8x8_cad_EndTransfer(u8x8);
}

void u8x8_d_st756x_set_contrast(u8x8_t* u8x8, int8_t contrast_offset) {
    // TODO: Tune contrast values and offset scaling for SSD1306/1315
    uint8_t contrast = (furi_hal_version_get_hw_display() == FuriHalVersionDisplayMgg) ?
                           CONTRAST_MGG :
                           CONTRAST_ERC;
    int16_t new_contrast = (int16_t)contrast + (int16_t)contrast_offset * 4;
    if(new_contrast < 0) new_contrast = 0;
    if(new_contrast > 255) new_contrast = 255;

    u8x8_cad_StartTransfer(u8x8);
    u8x8_cad_SendCmd(u8x8, SSD1306_CMD_SET_CONTRAST);
    u8x8_cad_SendArg(u8x8, (uint8_t)new_contrast);
    u8x8_cad_EndTransfer(u8x8);
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
            // TODO: SSD1306/1315 - regulation_ratio and bias params are ignored
            // Contrast values may need tuning per display variant
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
