// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/listnode.h>
#include <zircon/types.h>
#include <threads.h>
#include "dsi.h"

#define ADV7533_REG_CHIP_REVISION           0x00
#define ADV7533_REG_POWER                   0x41
#define ADV7533_REG_STATUS                  0x42
#define ADV7533_REG_EDID_I2C_ADDR           0x43
#define ADV7533_REG_PACKET_ENABLE1          0x44
#define ADV7533_REG_PACKET_I2C_ADDR         0x45
#define ADV7533_REG_INT0_ENABLE             0x94
#define ADV7533_REG_INT1_ENABLE             0x95
#define ADV7533_REG_INT0                    0x96
#define ADV7533_REG_INT1                    0x96
#define ADV7533_REG_HDCP_HDMI_CFG           0xaf
#define ADV7533_REG_EDID_SEGMENT            0xc4
#define ADV7533_REG_DDC_STATUS              0xc8
#define ADV7533_REG_EDID_READ_CTRL          0xc9
#define ADV7533_REG_POWER2                  0xd6
#define ADV7533_REG_CEC_I2C_ADDR            0xe1
#define ADV7533_REG_CEC_CTRL                0xe2
#define ADV7533_REG_CHIP_ID_HIGH            0xf5
#define ADV7533_REG_CHIP_ID_LOW             0xf6

#define EDID_I2C_ADDR                       (0x3B << 1)
#define PACKET_I2C_ADDR                     (0x34 << 1)
#define CEC_I2C_ADDR                        (0x38 << 1)

/* ADV7533_REG_POWER Bit Definitions */
#define REG_POWER_PWR_UP                    (0x10)
#define REG_POWER_PWR_DWN                   (0x50)

/* ADV7533_REG_STATUS Bit Definitions */
#define REG_STATUS_HPD_DET                  (1 << 6)
#define REG_STATUS_MON_SNS_DET              (1 << 5)


/* ADV7533_REG_PACKET_ENABLE1 Bit Definitions */
#define PACKET_ENABLE_DISABLE               (0x0)

/* ADV7533_REG_INT_ENABLE Bit Definitions */
#define REG_INT0_ENABLE_HPD                 (1 << 7)
#define REG_INT0_ENABLE_EDID_RDY            (1 << 2)
#define REG_INT1_ENABLE_DDC_ERR             (1 << 7)

/* ADV7533_REG_INT0 Bit Definitions */
#define REG_INT0_HPD                        (1 << 7)

/* ADV7533_REG_POWER2 Bit Definitions */
#define REG_POWER2_HPD_ALWAYS_HIGH          (0xC0)

/* ADV7533_REG_DDC_STATUS Bit Definition */
#define REG_DDC_STATUS_EDID_READY           (0x2)

/* ADV7533_REG_HDCP_HDMI_CFG Bit Definition */
#define REG_HDCP_HDMI_CFG_AVI_MODE          (0 << 1)
#define REG_HDCP_HDMI_CFG_HDMI_MODE         (1 << 1)
#define REG_HDCP_HDMI_CFG_DEFAULT           (0x14)
#define REG_HDCP_HDMI_CFG_ENB_HDMI          (REG_HDCP_HDMI_CFG_DEFAULT | \
                                                REG_HDCP_HDMI_CFG_HDMI_MODE)
#define REG_HDCP_HDMI_CFG_ENB_AVI           (REG_HDCP_HDMI_CFG_DEFAULT | \
                                                REG_HDCP_HDMI_CFG_AVI_MODE)

static void adv7533_mainchn_write(dsi_t* dsi, uint8_t d1, uint8_t d2);
static void adv7533_mainchn_read(dsi_t* dsi, uint8_t d1, uint8_t len);
static void adv7533_cecchn_write(dsi_t* dsi, uint8_t d1, uint8_t d2);
static void adv7533_edidchn_read(dsi_t* dsi, uint8_t d1, uint8_t len);
zx_status_t adv7533_init(dsi_t* dsi);
void hdmi_init(dsi_t* dsi);
uint8_t* adv7533_get_edid_buffer(void);