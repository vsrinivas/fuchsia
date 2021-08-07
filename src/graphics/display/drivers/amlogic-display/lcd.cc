// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lcd.h"

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/device-protocol/display-panel.h>
#include <lib/mipi-dsi/mipi-dsi.h>

#include <ddktl/device.h>

#include "common.h"

#define READ_DISPLAY_ID_CMD (0x04)
#define READ_DISPLAY_ID_LEN (0x03)

namespace amlogic_display {

namespace {
#include "initcodes-inl.h"
}  // namespace

zx_status_t Lcd::GetDisplayId(ddk::DsiImplProtocolClient dsiimpl, uint32_t* out) {
  ZX_ASSERT(out != nullptr);
  uint8_t txcmd = READ_DISPLAY_ID_CMD;
  uint8_t rsp[READ_DISPLAY_ID_LEN];
  zx_status_t status = ZX_OK;
  // Create the command using mipi-dsi library
  mipi_dsi_cmd_t cmd;
  status = mipi_dsi::MipiDsi::CreateCommand(&txcmd, 1, rsp, READ_DISPLAY_ID_LEN, COMMAND_GEN, &cmd);
  if (status == ZX_OK) {
    if ((status = dsiimpl.SendCmd(&cmd, 1)) != ZX_OK) {
      DISP_ERROR("Could not read out Display ID\n");
      return status;
    }
    *out = rsp[0] << 16 | rsp[1] << 8 | rsp[2];
  } else {
    DISP_ERROR("Invalid command (%d)\n", status);
  }

  return status;
}

zx_status_t Lcd::GetDisplayId() {
  uint32_t id;
  auto status = Lcd::GetDisplayId(dsiimpl_, &id);
  if (status == ZX_OK) {
    DISP_INFO("Display ID: 0x%x\n", id);
  }
  return status;
}

// This function write DSI commands based on the input buffer.
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
  ZX_DEBUG_ASSERT(initialized_);
  if (!enabled_) {
    return ZX_OK;
  }
  // First send shutdown command to LCD
  enabled_ = false;
  return LoadInitTable(lcd_shutdown_sequence, sizeof(lcd_shutdown_sequence));
}

zx_status_t Lcd::Enable() {
  ZX_DEBUG_ASSERT(initialized_);
  if (enabled_) {
    return ZX_OK;
  }
  // reset LCD panel via GPIO according to vendor doc
  gpio_.ConfigOut(1);
  gpio_.Write(1);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(30)));
  gpio_.Write(0);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
  gpio_.Write(1);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
  // check status
  if (GetDisplayId() != ZX_OK) {
    DISP_ERROR("Cannot communicate with LCD Panel!\n");
    return ZX_ERR_TIMED_OUT;
  }
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  // load table
  zx_status_t status;
  if (panel_type_ == PANEL_TV070WSM_FT) {
    status = LoadInitTable(lcd_init_sequence_TV070WSM_FT, sizeof(lcd_init_sequence_TV070WSM_FT));
  } else if (panel_type_ == PANEL_P070ACB_FT) {
    status = LoadInitTable(lcd_init_sequence_P070ACB_FT, sizeof(lcd_init_sequence_P070ACB_FT));
  } else if (panel_type_ == PANEL_TV101WXM_FT) {
    status = LoadInitTable(lcd_init_sequence_TV101WXM_FT, sizeof(lcd_init_sequence_TV101WXM_FT));
  } else if (panel_type_ == PANEL_G101B158_FT) {
    status = LoadInitTable(lcd_init_sequence_G101B158_FT, sizeof(lcd_init_sequence_G101B158_FT));
  } else if (panel_type_ == PANEL_TV080WXM_FT) {
    status = LoadInitTable(lcd_init_sequence_TV080WXM_FT, sizeof(lcd_init_sequence_TV080WXM_FT));
  } else if (panel_type_ == PANEL_TV101WXM_FT_9365) {
    status = LoadInitTable(lcd_init_sequence_TV101WXM_FT_9365,
                           sizeof(lcd_init_sequence_TV101WXM_FT_9365));
  } else if (panel_type_ == PANEL_TV070WSM_FT_9365) {
    status = LoadInitTable(lcd_init_sequence_TV070WSM_FT_9365,
                           sizeof(lcd_init_sequence_TV070WSM_FT_9365));
  } else if (panel_type_ == PANEL_KD070D82_FT || panel_type_ == PANEL_KD070D82_FT_9365) {
    status = LoadInitTable(lcd_init_sequence_KD070D82_FT_9365,
                           sizeof(lcd_init_sequence_KD070D82_FT_9365));
  } else if (panel_type_ == PANEL_TV070WSM_ST7703I) {
    status = LoadInitTable(lcd_init_sequence_TV070WSM_ST7703I,
                           sizeof(lcd_init_sequence_TV070WSM_ST7703I));
  } else {
    DISP_ERROR("Unsupported panel detected!\n");
    status = ZX_ERR_NOT_SUPPORTED;
  }

  if (status == ZX_OK) {
    // LCD is on now.
    enabled_ = true;
  }
  return status;
}

zx_status_t Lcd::Init(ddk::DsiImplProtocolClient dsiimpl, ddk::GpioProtocolClient gpio) {
  if (initialized_) {
    return ZX_OK;
  }

  dsiimpl_ = dsiimpl;

  if (!gpio.is_valid()) {
    DISP_ERROR("Could not obtain GPIO protocol\n");
    return ZX_ERR_NO_RESOURCES;
  }
  gpio_ = gpio;

  initialized_ = true;

  if (kBootloaderDisplayEnabled) {
    DISP_INFO("LCD Enabled by Bootloader. Disabling before proceeding\n");
    enabled_ = true;
    Disable();
  }

  return ZX_OK;
}

}  // namespace amlogic_display
