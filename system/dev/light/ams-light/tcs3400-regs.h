// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off
/* Regsiter Map */
#define TCS_I2C_ENABLE                   0x80    /* R/W Enables states and interrupts */
#define TCS_I2C_ATIME                    0x81    /* R/W RGBC integration time */
#define TCS_I2C_WTIME                    0x83    /* R/W Wait time */
#define TCS_I2C_AILTL                    0x84    /* R/W Clear interrupt low threshold low byte */
#define TCS_I2C_AILTH                    0x85    /* R/W Clear interrupt low threshold high byte */
#define TCS_I2C_AIHTL                    0x86    /* R/W Clear interrupt high threshold low byte */
#define TCS_I2C_AIHTH                    0x87    /* R/W Clear interrupt high threshold high byte */
#define TCS_I2C_PERS                     0x8C    /* R/W Interrupt persistence filter */
#define TCS_I2C_CONFIG                   0x8D    /* R/W Configuration */
#define TCS_I2C_CONTROL                  0x8F    /* R/W Gain control register */
#define TCS_I2C_AUX                      0x90    /* R/W Auxiliary control register */
#define TCS_I2C_REVID                    0x91    /* R Revision ID */
#define TCS_I2C_ID                       0x92    /* R Device ID */
#define TCS_I2C_STATUS                   0x93    /* R Device status */
#define TCS_I2C_CDATAL                   0x94    /* R Clear / IR channel low data register */
#define TCS_I2C_CDATAH                   0x95    /* R Clear / IR channel high data register */
#define TCS_I2C_RDATAL                   0x96    /* R Red ADC low data register */
#define TCS_I2C_RDATAH                   0x97    /* R Red ADC high data register */
#define TCS_I2C_GDATAL                   0x98    /* R Green ADC low data register */
#define TCS_I2C_GDATAH                   0x99    /* R Green ADC high data register */
#define TCS_I2C_BDATAL                   0x9A    /* R Blue ADC low data register */
#define TCS_I2C_BDATAH                   0x9B    /* R Blue ADC high data register */
#define TCS_I2C_IR                       0xC0    /* R/W Access IR Channel */
#define TCS_I2C_IFORCE                   0xE4    /* W Force Interrupt */
#define TCS_I2C_CICLEAR                  0xE6    /* W Clear channel interrupt clear */
#define TCS_I2C_AICLEAR                  0xE7    /* W Clear all interrupts */

#define TCS_I2C_BIT(shift) (uint8_t)(1u << shift)

#define TCS_I2C_ENABLE_POWER_ON                     TCS_I2C_BIT(0)
#define TCS_I2C_ENABLE_ADC_ENABLE                   TCS_I2C_BIT(1)
#define TCS_I2C_ENABLE_WAIT_ENABLE                  TCS_I2C_BIT(3)
#define TCS_I2C_ENABLE_INT_ENABLE                   TCS_I2C_BIT(4)
#define TCS_I2C_ENABLE_SLEEP_AFTER_INT              TCS_I2C_BIT(6)
// clang-format on
