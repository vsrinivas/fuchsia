// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lcd.h"

#include <ddk/debug.h>
#include <ddktl/device.h>
#include <lib/mipi-dsi/mipi-dsi.h>

#include "common.h"

#define DELAY_CMD (0xFF)
#define DCS_CMD (0xFE)
#define GEN_CMD (0xFD)

constexpr uint8_t kId1Reg = 0xDA;
constexpr uint8_t kId2Reg = 0xDC;
constexpr uint16_t panel1_id = 0xA1A1;
constexpr uint16_t panel2_id = 0xB1B1;

namespace mt8167s_display {

namespace {
// Based on Vendor datasheet
// <CMD TYPE><LENGTH><DATA...>
// <DELAY_CMD><DELAY (ms)>
constexpr uint8_t lcd_shutdown_sequence[] = {
    DELAY_CMD, 5, DCS_CMD, 1, 0x28, DELAY_CMD, 30, DCS_CMD, 1, 0x10, DELAY_CMD, 150,
};

constexpr uint8_t lcd_init_sequence_ST7701S_1[] = {
    DCS_CMD,   6,       0xFF,    0x77, 0x01,      0x00, 0x00, 0x00,    DCS_CMD, 1,    0x11,
    DELAY_CMD, 120,     DCS_CMD, 6,    0xFF,      0x77, 0x01, 0x00,    0x00,    0x10, DCS_CMD,
    17,        0xB0,    0x40,    0xC9, 0x8F,      0x0D, 0x11, 0x07,    0x02,    0x09, 0x09,
    0x1F,      0x04,    0x50,    0x0F, 0xE4,      0x29, 0xDF, DCS_CMD, 17,      0xB1, 0x40,
    0xCB,      0xD3,    0x11,    0x8F, 0x04,      0x00, 0x08, 0x07,    0x1C,    0x06, 0x53,
    0x12,      0x63,    0xEB,    0xDF, DCS_CMD,   6,    0xFF, 0x77,    0x01,    0x00, 0x00,
    0x00,      DCS_CMD, 1,       0x29, DELAY_CMD, 20,
};

constexpr uint8_t lcd_init_sequence_ST7701S_2[] = {
    DCS_CMD, 1, 0x11, DELAY_CMD, 120, DCS_CMD, 1, 0x29, DELAY_CMD, 20,
};

constexpr uint8_t lcd_init_sequence_ILI9881C[] = {
    DCS_CMD, 4,    0xFF,    0x98,      0x81,    0x03,    DCS_CMD, 2,    0x01,      0x00,
    DCS_CMD, 2,    0x02,    0x00,      DCS_CMD, 2,       0x03,    0x53, DCS_CMD,   2,
    0x04,    0x13, DCS_CMD, 2,         0x05,    0x13,    DCS_CMD, 2,    0x06,      0x06,
    DCS_CMD, 2,    0x07,    0x00,      DCS_CMD, 2,       0x08,    0x04, DCS_CMD,   2,
    0x09,    0x00, DCS_CMD, 2,         0x0a,    0x00,    DCS_CMD, 2,    0x0b,      0x00,
    DCS_CMD, 2,    0x0c,    0x00,      DCS_CMD, 2,       0x0d,    0x00, DCS_CMD,   2,
    0x0e,    0x00, DCS_CMD, 2,         0x0f,    0x00,    DCS_CMD, 2,    0x10,      0x00,
    DCS_CMD, 2,    0x11,    0x00,      DCS_CMD, 2,       0x12,    0x00, DCS_CMD,   2,
    0x13,    0x00, DCS_CMD, 2,         0x14,    0x00,    DCS_CMD, 2,    0x15,      0x00,
    DCS_CMD, 2,    0x16,    0x00,      DCS_CMD, 2,       0x17,    0x00, DCS_CMD,   2,
    0x18,    0x00, DCS_CMD, 2,         0x19,    0x00,    DCS_CMD, 2,    0x1a,      0x00,
    DCS_CMD, 2,    0x1b,    0x00,      DCS_CMD, 2,       0x1c,    0x00, DCS_CMD,   2,
    0x1d,    0x00, DCS_CMD, 2,         0x1e,    0xC0,    DCS_CMD, 2,    0x1f,      0x80,
    DCS_CMD, 2,    0x20,    0x04,      DCS_CMD, 2,       0x21,    0x0B, DCS_CMD,   2,
    0x22,    0x00, DCS_CMD, 2,         0x23,    0x00,    DCS_CMD, 2,    0x24,      0x00,
    DCS_CMD, 2,    0x25,    0x00,      DCS_CMD, 2,       0x26,    0x00, DCS_CMD,   2,
    0x27,    0x00, DCS_CMD, 2,         0x28,    0x55,    DCS_CMD, 2,    0x29,      0x03,
    DCS_CMD, 2,    0x2a,    0x00,      DCS_CMD, 2,       0x2b,    0x00, DCS_CMD,   2,
    0x2c,    0x00, DCS_CMD, 2,         0x2d,    0x00,    DCS_CMD, 2,    0x2e,      0x00,
    DCS_CMD, 2,    0x2f,    0x00,      DCS_CMD, 2,       0x30,    0x00, DCS_CMD,   2,
    0x31,    0x00, DCS_CMD, 2,         0x32,    0x00,    DCS_CMD, 2,    0x33,      0x00,
    DCS_CMD, 2,    0x34,    0x04,      DCS_CMD, 2,       0x35,    0x05, DCS_CMD,   2,
    0x36,    0x05, DCS_CMD, 2,         0x37,    0x00,    DCS_CMD, 2,    0x38,      0x3C,
    DCS_CMD, 2,    0x39,    0x00,      DCS_CMD, 2,       0x3a,    0x40, DCS_CMD,   2,
    0x3b,    0x40, DCS_CMD, 2,         0x3c,    0x00,    DCS_CMD, 2,    0x3d,      0x00,
    DCS_CMD, 2,    0x3e,    0x00,      DCS_CMD, 2,       0x3f,    0x00, DCS_CMD,   2,
    0x40,    0x00, DCS_CMD, 2,         0x41,    0x00,    DCS_CMD, 2,    0x42,      0x00,
    DCS_CMD, 2,    0x43,    0x00,      DCS_CMD, 2,       0x44,    0x00, DCS_CMD,   2,
    0x50,    0x01, DCS_CMD, 2,         0x51,    0x23,    DCS_CMD, 2,    0x52,      0x45,
    DCS_CMD, 2,    0x53,    0x67,      DCS_CMD, 2,       0x54,    0x89, DCS_CMD,   2,
    0x55,    0xAB, DCS_CMD, 2,         0x56,    0x01,    DCS_CMD, 2,    0x57,      0x23,
    DCS_CMD, 2,    0x58,    0x45,      DCS_CMD, 2,       0x59,    0x67, DCS_CMD,   2,
    0x5A,    0x89, DCS_CMD, 2,         0x5B,    0xAB,    DCS_CMD, 2,    0x5C,      0xCD,
    DCS_CMD, 2,    0x5D,    0xEF,      DCS_CMD, 2,       0x5E,    0x01, DCS_CMD,   2,
    0x5F,    0x14, DCS_CMD, 2,         0x60,    0x15,    DCS_CMD, 2,    0x61,      0x0C,
    DCS_CMD, 2,    0x62,    0x0D,      DCS_CMD, 2,       0x63,    0x0E, DCS_CMD,   2,
    0x64,    0x0F, DCS_CMD, 2,         0x65,    0x10,    DCS_CMD, 2,    0x66,      0x11,
    DCS_CMD, 2,    0x67,    0x08,      DCS_CMD, 2,       0x68,    0x02, DCS_CMD,   2,
    0x69,    0x0A, DCS_CMD, 2,         0x6A,    0x02,    DCS_CMD, 2,    0x6B,      0x02,
    DCS_CMD, 2,    0x6C,    0x02,      DCS_CMD, 2,       0x6D,    0x02, DCS_CMD,   2,
    0x6E,    0x02, DCS_CMD, 2,         0x6F,    0x02,    DCS_CMD, 2,    0x70,      0x02,
    DCS_CMD, 2,    0x71,    0x02,      DCS_CMD, 2,       0x72,    0x06, DCS_CMD,   2,
    0x73,    0x02, DCS_CMD, 2,         0x74,    0x02,    DCS_CMD, 2,    0x75,      0x14,
    DCS_CMD, 2,    0x76,    0x15,      DCS_CMD, 2,       0x77,    0x11, DCS_CMD,   2,
    0x78,    0x10, DCS_CMD, 2,         0x79,    0x0F,    DCS_CMD, 2,    0x7A,      0x0E,
    DCS_CMD, 2,    0x7B,    0x0D,      DCS_CMD, 2,       0x7C,    0x0C, DCS_CMD,   2,
    0x7D,    0x06, DCS_CMD, 2,         0x7E,    0x02,    DCS_CMD, 2,    0x7F,      0x0A,
    DCS_CMD, 2,    0x80,    0x02,      DCS_CMD, 2,       0x81,    0x02, DCS_CMD,   2,
    0x82,    0x02, DCS_CMD, 2,         0x83,    0x02,    DCS_CMD, 2,    0x84,      0x02,
    DCS_CMD, 2,    0x85,    0x02,      DCS_CMD, 2,       0x86,    0x02, DCS_CMD,   2,
    0x87,    0x02, DCS_CMD, 2,         0x88,    0x08,    DCS_CMD, 2,    0x89,      0x02,
    DCS_CMD, 2,    0x8A,    0x02,      DCS_CMD, 4,       0xFF,    0x98, 0x81,      0x04,
    DCS_CMD, 2,    0x6C,    0x15,      DCS_CMD, 2,       0x6E,    0x3B, DCS_CMD,   2,
    0x6F,    0x53, DCS_CMD, 2,         0x3A,    0xA4,    DCS_CMD, 2,    0x8D,      0x15,
    DCS_CMD, 2,    0x87,    0xBA,      DCS_CMD, 2,       0x26,    0x76, DCS_CMD,   2,
    0xB2,    0xD1, DCS_CMD, 2,         0x88,    0x0B,    DCS_CMD, 4,    0xFF,      0x98,
    0x81,    0x01, DCS_CMD, 2,         0x22,    0x0A,    DCS_CMD, 2,    0x31,      0x00,
    DCS_CMD, 2,    0x53,    0x96,      DCS_CMD, 2,       0x55,    0x88, DCS_CMD,   2,
    0x50,    0x96, DCS_CMD, 2,         0x51,    0x96,    DCS_CMD, 2,    0x60,      0x14,
    DCS_CMD, 2,    0xA0,    0x08,      DCS_CMD, 2,       0xA1,    0x1C, DCS_CMD,   2,
    0xA2,    0x29, DCS_CMD, 2,         0xA3,    0x13,    DCS_CMD, 2,    0xA4,      0x16,
    DCS_CMD, 2,    0xA5,    0x28,      DCS_CMD, 2,       0xA6,    0x1C, DCS_CMD,   2,
    0xA7,    0x1D, DCS_CMD, 2,         0xA8,    0x80,    DCS_CMD, 2,    0xA9,      0x1a,
    DCS_CMD, 2,    0xAA,    0x27,      DCS_CMD, 2,       0xAB,    0x6A, DCS_CMD,   2,
    0xAC,    0x1a, DCS_CMD, 2,         0xAD,    0x19,    DCS_CMD, 2,    0xAE,      0x4b,
    DCS_CMD, 2,    0xAF,    0x21,      DCS_CMD, 2,       0xB0,    0x25, DCS_CMD,   2,
    0xB1,    0x4A, DCS_CMD, 2,         0xB2,    0x59,    DCS_CMD, 2,    0xB3,      0x2C,
    DCS_CMD, 2,    0xC0,    0x08,      DCS_CMD, 2,       0xC1,    0x1C, DCS_CMD,   2,
    0xC2,    0x29, DCS_CMD, 2,         0xC3,    0x13,    DCS_CMD, 2,    0xC4,      0x17,
    DCS_CMD, 2,    0xC5,    0x28,      DCS_CMD, 2,       0xC6,    0x1C, DCS_CMD,   2,
    0xC7,    0x1D, DCS_CMD, 2,         0xC8,    0x80,    DCS_CMD, 2,    0xC9,      0x1a,
    DCS_CMD, 2,    0xCA,    0x27,      DCS_CMD, 2,       0xCB,    0x6A, DCS_CMD,   2,
    0xCC,    0x1A, DCS_CMD, 2,         0xCD,    0x19,    DCS_CMD, 2,    0xCE,      0x4b,
    DCS_CMD, 2,    0xCF,    0x21,      DCS_CMD, 2,       0xD0,    0x25, DCS_CMD,   2,
    0xD1,    0x4A, DCS_CMD, 2,         0xD2,    0x5B,    DCS_CMD, 2,    0xD3,      0x2C,
    DCS_CMD, 4,    0xFF,    0x98,      0x81,    0x00,    DCS_CMD, 2,    0x35,      0x00,
    DCS_CMD, 1,    0x11,    DELAY_CMD, 120,     DCS_CMD, 1,       0x29, DELAY_CMD, 20,
};
}  // namespace

zx_status_t Lcd::GetDisplayId(uint16_t& id) {
  uint8_t id1;
  uint8_t id2;
  zx_status_t status = ZX_OK;

  // Create the command using mipi-dsi library
  mipi_dsi_cmd_t cmd[2];
  status = mipi_dsi::MipiDsi::CreateCommand(&kId1Reg, 1, &id1, 1, COMMAND_DCS, &cmd[0]);
  if (status != ZX_OK) {
    DISP_ERROR("Invalid command (%d)\n", status);
    return status;
  }

  status = mipi_dsi::MipiDsi::CreateCommand(&kId2Reg, 1, &id2, 1, COMMAND_DCS, &cmd[1]);
  if (status != ZX_OK) {
    DISP_ERROR("Invalid command (%d)\n", status);
    return status;
  }

  if ((status = dsiimpl_.SendCmd(cmd, 2)) != ZX_OK) {
    DISP_ERROR("Could not read out Display ID\n");
    return status;
  }
  id = static_cast<uint16_t>((id1 << 8) | id2);
  DISP_INFO("Display ID: 0x%x\n", id);
  return status;
}

zx_status_t Lcd::LoadInitTable(const uint8_t* buffer, size_t size) {
  zx_status_t status = ZX_OK;
  size_t i;
  i = 0;
  bool isDCS = false;
  while (i < size) {
    switch (buffer[i]) {
      case DELAY_CMD:
        zx_nanosleep(zx_deadline_after(ZX_MSEC(buffer[i + 1])));
        i += 2;
        break;
      case DCS_CMD:
        isDCS = true;
        __FALLTHROUGH;
      case GEN_CMD:
      default:
        // Create the command using mipi-dsi library
        mipi_dsi_cmd_t cmd;
        status =
            mipi_dsi::MipiDsi::CreateCommand(&buffer[i + 2], buffer[i + 1], NULL, 0, isDCS, &cmd);
        if (status == ZX_OK) {
          if ((status = dsiimpl_.SendCmd(&cmd, 1)) != ZX_OK) {
            DISP_ERROR("Error loading LCD init table. Aborting %d\n", status);
            return status;
          }
        } else {
          DISP_ERROR("Invalid command (%d). Skipping\n", status);
        }
        // increment by payload length
        i += buffer[i + 1] + 2;  // the 2 includes current plus size field
        isDCS = false;
        break;
    }
  }
  return status;
}

zx_status_t Lcd::Disable() {
  if (!enabled_) {
    return ZX_OK;
  }
  // First send shutdown command to LCD
  enabled_ = false;
  return LoadInitTable(lcd_shutdown_sequence, sizeof(lcd_shutdown_sequence));
}

void Lcd::PowerOn() {
  if (gpio_.is_valid()) {
    gpio_.ConfigOut(1);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
    gpio_.Write(0);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(200)));
    gpio_.Write(1);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
  }
}

void Lcd::PowerOff() {
  if (gpio_.is_valid()) {
    gpio_.Write(0);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(120)));
  }
}

zx_status_t Lcd::Enable() {
  if (enabled_) {
    return ZX_OK;
  }

  // load table
  zx_status_t status;
  if (panel_type_ == PANEL_ILI9881C) {
    status = LoadInitTable(lcd_init_sequence_ILI9881C, sizeof(lcd_init_sequence_ILI9881C));
  } else if (panel_type_ == PANEL_ST7701S) {
    // There are two types are panels. Identify them
    uint16_t id;
    status = GetDisplayId(id);
    if (status == ZX_OK) {
      if (id == panel1_id) {
        status = LoadInitTable(lcd_init_sequence_ST7701S_1, sizeof(lcd_init_sequence_ST7701S_1));
      } else if (id == panel2_id) {
        status = LoadInitTable(lcd_init_sequence_ST7701S_2, sizeof(lcd_init_sequence_ST7701S_2));
      } else {
        status = ZX_ERR_NOT_SUPPORTED;
      }
    } else {
      DISP_ERROR("Could not read display ID\n");
    }
  } else {
    status = ZX_ERR_NOT_SUPPORTED;
  }

  if (status == ZX_OK) {
    // LCD is on now.
    enabled_ = true;
  } else {
    DISP_ERROR("Unsupported panel detected\n");
  }

  return status;
}

}  // namespace mt8167s_display
