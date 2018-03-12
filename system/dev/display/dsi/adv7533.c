// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <hw/reg.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/gpio.h>

#include "dsi.h"
#include "adv7533.h"
#include "edid.h"

#define TRACE zxlogf(INFO, "%s %d\n", __FUNCTION__, __LINE__);

static const uint8_t adv7533_fixed_registers[] = {
    0x16, 0x20,
    0x9a, 0xe0,
    0xba, 0x70,
    0xde, 0x82,
    0xe4, 0x40,
    0xe5, 0x80
};

static const uint8_t adv7533_cec_fixed_registers[] = {
    0x15, 0xd0 ,
    0x17, 0xd0 ,
    0x24, 0x20 ,
    0x57, 0x11 ,
    0x05, 0xc8
};

uint8_t edid_buf[256];
bool edid_complete = false;

uint8_t* adv7533_get_edid_buffer(void) {
    if (edid_complete) {
        return edid_buf;
    }
    return 0;
}

/* Helper functions for reading/writing to single registers */
static void adv7533_mainchn_write(dsi_t* dsi, uint8_t d1, uint8_t d2) {
    dsi->write_buf[0] = d1;
    dsi->write_buf[1] = d2;
    i2c_transact_sync(&dsi->i2c_dev.i2c, I2C_MAIN, dsi->write_buf, 2, NULL, 0);
}

static void adv7533_mainchn_read(dsi_t* dsi, uint8_t d1, uint8_t len) {
    dsi->write_buf[0] = d1;
    i2c_transact_sync(&dsi->i2c_dev.i2c, I2C_MAIN, dsi->write_buf, 1, dsi->write_buf, len);
}

static void adv7533_cecchn_write(dsi_t* dsi, uint8_t d1, uint8_t d2) {
    dsi->write_buf[0] = d1;
    dsi->write_buf[1] = d2;
    i2c_transact_sync(&dsi->i2c_dev.i2c, I2C_CEC, dsi->write_buf, 2, NULL, 0);
}

static void adv7533_edidchn_read(dsi_t* dsi, uint8_t d1, uint8_t len) {
    dsi->write_buf[0] = d1;
    i2c_transact_sync(&dsi->i2c_dev.i2c, I2C_EDID, dsi->write_buf, 1, dsi->write_buf, len);
}

zx_status_t adv7533_init(dsi_t* dsi) {
    gpio_protocol_t* gpio = &dsi->hdmi_gpio.gpio;

    adv7533_mainchn_read(dsi, ADV7533_REG_CHIP_REVISION, 1);
    zxlogf(INFO, "%s: HDMI Ver 0x%x\n", __FUNCTION__, dsi->write_buf[0]);

    /* Write ADV7533 fixed register values */
    for (size_t i = 0; i < sizeof(adv7533_fixed_registers); i += 2) {
        adv7533_mainchn_write(dsi, adv7533_fixed_registers[i], adv7533_fixed_registers[i + 1]);
    }

    /* Write EDID I2C Slave Address */
    adv7533_mainchn_write(dsi, ADV7533_REG_EDID_I2C_ADDR, EDID_I2C_ADDR);

    /* Write packet i2c address */
    adv7533_mainchn_write(dsi, ADV7533_REG_PACKET_I2C_ADDR, PACKET_I2C_ADDR);

    /* Write cec i2c address */
    adv7533_mainchn_write(dsi, ADV7533_REG_CEC_I2C_ADDR, CEC_I2C_ADDR);

    /* Disable packet_enable1 */
    adv7533_mainchn_write(dsi, ADV7533_REG_PACKET_ENABLE1, PACKET_ENABLE_DISABLE);

    /* Write ADV7533 CEC fixed register values */
    for (size_t i = 0; i < sizeof(adv7533_cec_fixed_registers); i += 2) {
        adv7533_mainchn_write(dsi, adv7533_cec_fixed_registers[i],
            adv7533_cec_fixed_registers[i + 1]);
    }

    /* ADV7533_REG_CEC_CTRL write 1 */
    adv7533_mainchn_write(dsi, ADV7533_REG_CEC_CTRL, 0x1);

    /* Power off dsi */
    /* Spec doc missing for these magic registers */
    adv7533_cecchn_write(dsi, 0x3, 0xb);
    adv7533_cecchn_write(dsi, 0x27, 0xb);

    /* Detect display */
    /* TODO: Once GPIO IRQ support is added, we can properly implement hotplug detection */
    adv7533_mainchn_read(dsi, ADV7533_REG_STATUS, 1);
    if ( (dsi->write_buf[0] & REG_STATUS_HPD_DET) == 0) {
        zxlogf(INFO, "%s: No external display detected\n", __FUNCTION__);
        return ZX_ERR_IO_NOT_PRESENT;
    }

    /* Clear HPD INTR */
    adv7533_mainchn_write(dsi, ADV7533_REG_INT0, REG_INT0_HPD);

    /* Power up interface */
    adv7533_mainchn_write(dsi, ADV7533_REG_POWER, REG_POWER_PWR_UP);

    /* Enable HPD and EDID RDY Interrupt */
    adv7533_mainchn_write(dsi, ADV7533_REG_INT0_ENABLE,
        (REG_INT0_ENABLE_HPD | REG_INT0_ENABLE_EDID_RDY));

    /* Enable DDC Errors */
    adv7533_mainchn_write(dsi, ADV7533_REG_INT1_ENABLE, REG_INT1_ENABLE_DDC_ERR);

    /* Assume HPD is always HIGH (ignore HPD line) */
    adv7533_mainchn_write(dsi, ADV7533_REG_POWER2, REG_POWER2_HPD_ALWAYS_HIGH);

    /* Set EDID I2C Slave Address */
    adv7533_mainchn_write(dsi, ADV7533_REG_EDID_I2C_ADDR, EDID_I2C_ADDR);

    /* Wait EDID once ready */
    //TODO: Use GPIO IRQ once it is implemented
    uint8_t g = 0;
    do {
        gpio_read(gpio, GPIO_INT, &g);
    } while(g);

    /* Interrupt fired. Let's see if EDID is ready to be read */
    adv7533_mainchn_read(dsi, ADV7533_REG_DDC_STATUS, 1);
    if (dsi->write_buf[0] != REG_DDC_STATUS_EDID_READY) {
        zxlogf(ERROR, "%s: EDID not ready!!!!\n", __FUNCTION__);
        adv7533_mainchn_read(dsi, ADV7533_REG_INT0, 2);
        zxlogf(ERROR, "%s: INTR REGS: ADV7533_REG_INT0 = 0x%x, ADV7533_REG_INT1 = 0x%x\n",
            __FUNCTION__, dsi->write_buf[0], dsi->write_buf[1]);
        return ZX_ERR_INTERNAL;
    }

    // Save EDID value. Read first 128 first
    for (int h = 0; h < 128; h += 32) {
        adv7533_edidchn_read(dsi, h, 32);
        memcpy(&edid_buf[h], &dsi->write_buf[0], 32);
    }

    if (edid_has_extension(edid_buf)) {
        zxlogf(INFO, "EDID has extension. Continue Reading\n");
        for (int h = 128; h < 256; h += 32) {
            adv7533_edidchn_read(dsi, h, 32);
            memcpy(&edid_buf[h], &dsi->write_buf[0], 32);
        }
    }
    edid_complete = true;

    /* Power down interface for now */
    adv7533_mainchn_write(dsi, ADV7533_REG_POWER, REG_POWER_PWR_DWN);

    /* Enable HDMI Mode */
    adv7533_mainchn_write(dsi, ADV7533_REG_HDCP_HDMI_CFG, REG_HDCP_HDMI_CFG_ENB_HDMI);

    return ZX_OK;
}

void hdmi_init(dsi_t* dsi) {

    /* Power up the interface */
    adv7533_mainchn_write(dsi, ADV7533_REG_POWER, REG_POWER_PWR_UP);

    /* Enable HPD and EDID RDY Interrupt */
    adv7533_mainchn_write(dsi, ADV7533_REG_INT0_ENABLE,
        (REG_INT0_ENABLE_HPD | REG_INT0_ENABLE_EDID_RDY));

    /* Enable DDC Errors */
    adv7533_mainchn_write(dsi, ADV7533_REG_INT1_ENABLE, REG_INT1_ENABLE_DDC_ERR);

    /* Write ADV7533 fixed register values */
    for (size_t i = 0; i < sizeof(adv7533_fixed_registers); i += 2) {
        adv7533_mainchn_write(dsi, adv7533_fixed_registers[i], adv7533_fixed_registers[i + 1]);
    }

    /* Write EDID I2C Slave Address */
    adv7533_mainchn_write(dsi, ADV7533_REG_EDID_I2C_ADDR, EDID_I2C_ADDR);

    /* Write packet i2c address */
    adv7533_mainchn_write(dsi, ADV7533_REG_PACKET_I2C_ADDR, PACKET_I2C_ADDR);

    /* Write cec i2c address */
    adv7533_mainchn_write(dsi, ADV7533_REG_CEC_I2C_ADDR, CEC_I2C_ADDR);

    /* Enable HDMI Mode */
    adv7533_mainchn_write(dsi, ADV7533_REG_HDCP_HDMI_CFG, REG_HDCP_HDMI_CFG_ENB_HDMI);

    /* Assume HPD is always HIGH (ignore HPD line) */
    adv7533_mainchn_write(dsi, ADV7533_REG_POWER2, REG_POWER2_HPD_ALWAYS_HIGH);

    /* set number of dsi lanes */
    adv7533_cecchn_write(dsi, 0x1c, 0x40);

    /* disable internal timing generator */
    adv7533_cecchn_write(dsi, 0x27, 0xb);
    /* enable hdmi */
    adv7533_cecchn_write(dsi, 0x3, 0x89);

    /* disable test mode */
    adv7533_cecchn_write(dsi, 0x55, 0x0);

    /* Write ADV7533 CEC fixed register values */
    for (size_t i = 0; i < sizeof(adv7533_cec_fixed_registers); i += 2) {
        adv7533_mainchn_write(dsi, adv7533_cec_fixed_registers[i],
            adv7533_cec_fixed_registers[i + 1]);
    }
}
