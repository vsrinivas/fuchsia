// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "adv7533.h"

#include <lib/device-protocol/i2c.h>
#include <lib/edid/edid.h>

#include "common.h"
#include "edid.h"
#include "hidisplay-regs.h"
#include "hidisplay.h"

namespace hi_display {

uint8_t edid_buf_[256];
constexpr uint32_t kAdvDelay = 1000;
constexpr uint32_t kAdvTimeout = 100000;

void Adv7533::HdmiGpioInit() {
  gpio_config_out(&gpios[GPIO_MUX], 0);
  gpio_config_out(&gpios[GPIO_PD], 0);
  gpio_config_in(&gpios[GPIO_INT], GPIO_NO_PULL);
  gpio_write(&gpios[GPIO_MUX], 0);
}

/* Helper functions for reading/writing to single registers */
void Adv7533::Adv7533MainChannelWrite(uint8_t d1, uint8_t d2) {
  write_buf_[0] = d1;
  write_buf_[1] = d2;
  i2c_write_read_sync(&i2c_dev.i2c_main, write_buf_, 2, NULL, 0);
}

void Adv7533::Adv7533MainChannelRead(uint8_t d1, uint8_t len) {
  write_buf_[0] = d1;
  i2c_write_read_sync(&i2c_dev.i2c_main, write_buf_, 1, write_buf_, len);
}

void Adv7533::Adv7533CecChannelWrite(uint8_t d1, uint8_t d2) {
  write_buf_[0] = d1;
  write_buf_[1] = d2;
  i2c_write_read_sync(&i2c_dev.i2c_cec, write_buf_, 2, NULL, 0);
}

void Adv7533::Adv7533EdidChannelRead(uint8_t d1, uint8_t len) {
  write_buf_[0] = d1;
  i2c_write_read_sync(&i2c_dev.i2c_edid, write_buf_, 1, write_buf_, len);
}

zx_status_t Adv7533::Adv7533Init(pdev_protocol_t* pdev_) {
  uint8_t g = 0;

#if 0 // This needs to be rewritten to use composite protocol instead.
  zx_status_t status;
  size_t actual;

  if ((status = pdev_get_protocol(pdev_, ZX_PROTOCOL_I2C, 0, &i2c_dev.i2c_main,
                                  sizeof(i2c_dev.i2c_main), &actual)) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not obtain I2C Protocol\n", __FUNCTION__);
    return status;
  }
  if ((status = pdev_get_protocol(pdev_, ZX_PROTOCOL_I2C, 1, &i2c_dev.i2c_cec,
                                  sizeof(i2c_dev.i2c_cec), &actual)) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not obtain I2C Protocol\n", __FUNCTION__);
    return status;
  }
  if ((status = pdev_get_protocol(pdev_, ZX_PROTOCOL_I2C, 2, &i2c_dev.i2c_edid,
                                  sizeof(i2c_dev.i2c_edid), &actual)) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not obtain I2C Protocol\n", __FUNCTION__);
    return status;
  }

  /* Obtain the GPIO devices */
  for (uint32_t i = 0; i < countof(gpios); i++) {
    if ((status = pdev_get_protocol(pdev_, ZX_PROTOCOL_GPIO, i, &gpios[i], sizeof(gpios[i]),
                                    &actual)) != ZX_OK) {
      zxlogf(ERROR, "%s: Could not obtain GPIO Protocol\n", __FUNCTION__);
      return status;
    }
  }
#endif

  HdmiGpioInit();

  Adv7533MainChannelRead(ADV7533_REG_CHIP_REVISION, 1);
  zxlogf(INFO, "%s: HDMI Ver 0x%x\n", __FUNCTION__, write_buf_[0]);

  /* Write ADV7533 fixed register values */
  for (uint32_t i = 0; i < sizeof(kAdv7533FixedRegs); i += 2) {
    Adv7533MainChannelWrite(kAdv7533FixedRegs[i], kAdv7533FixedRegs[i + 1]);
  }

  /* Write EDID I2C Slave Address */
  Adv7533MainChannelWrite(ADV7533_REG_EDID_I2C_ADDR, EDID_I2C_ADDR);

  /* Write packet i2c address */
  Adv7533MainChannelWrite(ADV7533_REG_PACKET_I2C_ADDR, PACKET_I2C_ADDR);

  /* Write cec i2c address */
  Adv7533MainChannelWrite(ADV7533_REG_CEC_I2C_ADDR, CEC_I2C_ADDR);

  /* Disable packet_enable1 */
  Adv7533MainChannelWrite(ADV7533_REG_PACKET_ENABLE1, PACKET_ENABLE_DISABLE);

  /* Write ADV7533 CEC fixed register values */
  for (size_t i = 0; i < sizeof(kAdv7533CecFixedRegs); i += 2) {
    Adv7533MainChannelWrite(kAdv7533CecFixedRegs[i], kAdv7533CecFixedRegs[i + 1]);
  }

  /* ADV7533_REG_CEC_CTRL write 1 */
  Adv7533MainChannelWrite(ADV7533_REG_CEC_CTRL, 0x1);

  /* Spec doc missing for these magic registers */
  Adv7533CecChannelWrite(0x3, 0xb);
  Adv7533CecChannelWrite(0x27, 0xb);

  Adv7533MainChannelRead(ADV7533_REG_STATUS, 1);
  if ((write_buf_[0] & REG_STATUS_HPD_DET) == 0) {
    zxlogf(INFO, "%s: No external disp_intf_ detected\n", __FUNCTION__);
    return ZX_ERR_IO_NOT_PRESENT;
  }

  /* Clear HPD INTR */
  Adv7533MainChannelWrite(ADV7533_REG_INT0, REG_INT0_HPD);

  /* Power up interface */
  Adv7533MainChannelWrite(ADV7533_REG_POWER, REG_POWER_PWR_UP);

  /* Enable HPD and EDID RDY Interrupt */
  Adv7533MainChannelWrite(ADV7533_REG_INT0_ENABLE,
                          (REG_INT0_ENABLE_HPD | REG_INT0_ENABLE_EDID_RDY));

  /* Enable DDC Errors */
  Adv7533MainChannelWrite(ADV7533_REG_INT1_ENABLE, REG_INT1_ENABLE_DDC_ERR);

  /* Assume HPD is always HIGH (ignore HPD line) */
  Adv7533MainChannelWrite(ADV7533_REG_POWER2, REG_POWER2_HPD_ALWAYS_HIGH);

  /* Set EDID I2C Slave Address */
  Adv7533MainChannelWrite(ADV7533_REG_EDID_I2C_ADDR, EDID_I2C_ADDR);

  /* Wait EDID once ready */
  int timeout = kAdvTimeout;
  gpio_read(&gpios[GPIO_INT], &g);
  while (g && timeout--) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(kAdvDelay)));
    if (timeout <= 0) {
      zxlogf(ERROR, "Adv7533 EDID not ready\n");
      return ZX_ERR_TIMED_OUT;
    }
  }

  /* Interrupt fired. Let's see if EDID is ready to be read */
  Adv7533MainChannelRead(ADV7533_REG_DDC_STATUS, 1);
  if (write_buf_[0] != REG_DDC_STATUS_EDID_READY) {
    zxlogf(ERROR, "%s: EDID not ready!!!!\n", __FUNCTION__);
    Adv7533MainChannelRead(ADV7533_REG_INT0, 2);
    zxlogf(ERROR, "%s: INTR REGS: ADV7533_REG_INT0 = 0x%x, ADV7533_REG_INT1 = 0x%x\n", __FUNCTION__,
           write_buf_[0], write_buf_[1]);
    return ZX_ERR_INTERNAL;
  }

  // Save EDID value. Read first 128 first
  for (int h = 0; h < 128; h += 32) {
    Adv7533EdidChannelRead((uint8_t)h, 32);
    memcpy(&edid_buf_[h], &write_buf_[0], 32);
  }
  edid::BaseEdid* EdidBuf = (edid::BaseEdid*)edid_buf_;

  if (EdidBuf->num_extensions) {
    zxlogf(INFO, "EDID has extension. Continue Reading\n");
    for (int h = 128; h < 256; h += 32) {
      Adv7533EdidChannelRead((uint8_t)h, 32);
      memcpy(&edid_buf_[h], &write_buf_[0], 32);
    }
  }

  /* Power down interface for now */
  Adv7533MainChannelWrite(ADV7533_REG_POWER, REG_POWER_PWR_DWN);

  /* Enable HDMI Mode */
  Adv7533MainChannelWrite(ADV7533_REG_HDCP_HDMI_CFG, REG_HDCP_HDMI_CFG_ENB_HDMI);

  HdmiInit();

  return ZX_OK;
}

void Adv7533::Adv7533EnableTestMode() {
  /* enable internal timing generator */
  Adv7533CecChannelWrite(0x27, 0xcb);
  Adv7533CecChannelWrite(0x27, 0x8b);
  Adv7533CecChannelWrite(0x27, 0xcb);

  /* enable hdmi */
  Adv7533CecChannelWrite(0x3, 0x89);

  /* enable test mode */
  Adv7533CecChannelWrite(0x55, 0x80);
  Adv7533CecChannelWrite(0x03, 0x89);
  Adv7533CecChannelWrite(0xAF, 0x16);
}

void Adv7533::HdmiInit() {
  /* Power up the interface */
  Adv7533MainChannelWrite(ADV7533_REG_POWER, REG_POWER_PWR_UP);

  /* Enable HPD and EDID RDY Interrupt */
  Adv7533MainChannelWrite(ADV7533_REG_INT0_ENABLE,
                          (REG_INT0_ENABLE_HPD | REG_INT0_ENABLE_EDID_RDY));

  /* Enable DDC Errors */
  Adv7533MainChannelWrite(ADV7533_REG_INT1_ENABLE, REG_INT1_ENABLE_DDC_ERR);

  /* Write ADV7533 fixed register values */
  for (size_t i = 0; i < sizeof(kAdv7533FixedRegs); i += 2) {
    Adv7533MainChannelWrite(kAdv7533FixedRegs[i], kAdv7533FixedRegs[i + 1]);
  }

  /* Write EDID I2C Slave Address */
  Adv7533MainChannelWrite(ADV7533_REG_EDID_I2C_ADDR, EDID_I2C_ADDR);

  /* Write packet i2c address */
  Adv7533MainChannelWrite(ADV7533_REG_PACKET_I2C_ADDR, PACKET_I2C_ADDR);

  /* Write cec i2c address */
  Adv7533MainChannelWrite(ADV7533_REG_CEC_I2C_ADDR, CEC_I2C_ADDR);

  /* Enable HDMI Mode */
  Adv7533MainChannelWrite(ADV7533_REG_HDCP_HDMI_CFG, REG_HDCP_HDMI_CFG_ENB_HDMI);

  /* Assume HPD is always HIGH (ignore HPD line) */
  Adv7533MainChannelWrite(ADV7533_REG_POWER2, REG_POWER2_HPD_ALWAYS_HIGH);

  /* set number of disp_intf_ lanes */
  Adv7533CecChannelWrite(0x1c, 0x40);

  /* disable internal timing generator */
  Adv7533CecChannelWrite(0x27, 0xb);

  /* enable hdmi */
  Adv7533CecChannelWrite(0x3, 0x89);

  /* disable test mode */
  Adv7533CecChannelWrite(0x55, 0x0);

#ifdef DW_DSI_TEST_ENABLE
  Adv7533EnableTestMode();
#endif

  /* Write ADV7533 CEC fixed register values */
  for (size_t i = 0; i < sizeof(kAdv7533CecFixedRegs); i += 2) {
    Adv7533MainChannelWrite(kAdv7533CecFixedRegs[i], kAdv7533CecFixedRegs[i + 1]);
  }
}

}  // namespace hi_display
