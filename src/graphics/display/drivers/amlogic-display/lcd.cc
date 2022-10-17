// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/amlogic-display/lcd.h"

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/device-protocol/display-panel.h>
#include <lib/mipi-dsi/mipi-dsi.h>

#include <ddktl/device.h>
#include <fbl/alloc_checker.h>

#include "src/graphics/display/drivers/amlogic-display/common.h"
#include "src/graphics/display/drivers/amlogic-display/panel-config.h"

#define READ_DISPLAY_ID_CMD (0x04)
#define READ_DISPLAY_ID_LEN (0x03)

namespace amlogic_display {

namespace {

static zx_status_t CheckMipiReg(ddk::DsiImplProtocolClient* dsiimpl, uint8_t reg, size_t count) {
  ZX_DEBUG_ASSERT(count > 0);

  const uint8_t payload[3] = {kMipiDsiDtGenShortRead1, 1, reg};
  uint8_t rsp[count];
  mipi_dsi_cmd_t cmd{
      .virt_chn_id = kMipiDsiVirtualChanId,
      .dsi_data_type = kMipiDsiDtGenShortRead1,
      .pld_data_list = payload,
      .pld_data_count = 1,
      .rsp_data_list = rsp,
      .rsp_data_count = count,
      .flags = MIPI_DSI_CMD_FLAGS_ACK | MIPI_DSI_CMD_FLAGS_SET_MAX,
  };

  zx_status_t status;
  if ((status = dsiimpl->SendCmd(&cmd, 1)) != ZX_OK) {
    DISP_ERROR("Could not read register %c\n", reg);
    return status;
  }
  if (cmd.rsp_data_count != count) {
    DISP_ERROR("MIPI-DSI register read was short: got %zu want %zu. Ignoring", cmd.rsp_data_count,
               count);
  }
  return ZX_OK;
}

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
zx_status_t Lcd::LoadInitTable(cpp20::span<const uint8_t> buffer) {
  zx_status_t status = ZX_OK;
  uint32_t delay_ms = 0;
  constexpr size_t kMinCmdSize = 2;

  for (size_t i = 0; i < buffer.size() - kMinCmdSize;) {
    const uint8_t cmd_type = buffer[i];
    const uint8_t payload_size = buffer[i + 1];
    bool is_dcs = false;
    // This command has an implicit size=2, treat it specially.
    if (cmd_type == kDsiOpSleep) {
      if (payload_size == 0 || payload_size == 0xff) {
        return status;
      }
      zx::nanosleep(zx::deadline_after(zx::msec(payload_size)));
      i += 2;
      continue;
    }
    if (payload_size == 0) {
      i += kMinCmdSize;
      continue;
    }
    if ((i + payload_size + kMinCmdSize) > buffer.size()) {
      DISP_ERROR("buffer[%lu] command 0x%x size=0x%x would overflow buffer size=%lu", i, cmd_type,
                 payload_size, buffer.size());
      return ZX_ERR_OUT_OF_RANGE;
    }

    switch (cmd_type) {
      case kDsiOpDelay:
        delay_ms = 0;
        for (size_t j = 0; j < payload_size; j++) {
          delay_ms += buffer[i + 2 + j];
        }
        if (delay_ms > 0) {
          zx::nanosleep(zx::deadline_after(zx::msec(delay_ms)));
        }
        break;
      case kDsiOpGpio:
        DISP_TRACE("dsi_set_gpio size=%d value=%d", payload_size, buffer[i + 3]);
        if (buffer[i + 2] != 0) {
          DISP_ERROR("Unrecognized GPIO pin (%d)", buffer[i + 2]);
          // We _should_ bail here, but this spec-violating behavior is present
          // in the other drivers for this hardware.
          //
          // return ZX_ERR_UNKNOWN;
        } else {
          gpio_.ConfigOut(buffer[i + 3]);
        }
        if (payload_size > 2 && buffer[i + 4]) {
          DISP_TRACE("dsi_set_gpio sleep %d", buffer[i + 4]);
          zx::nanosleep(zx::deadline_after(zx::msec(buffer[i + 4])));
        }
        break;
      case kDsiOpReadReg:
        DISP_TRACE("dsi_read size=%d reg=%d, count=%d", payload_size, buffer[i + 2], buffer[i + 3]);
        if (payload_size != 2) {
          DISP_ERROR("Invalid MIPI-DSI reg check, expected a register and a target value!");
        }
        status = CheckMipiReg(&dsiimpl_, /*reg=*/buffer[i + 2], /*count=*/buffer[i + 3]);
        if (status != ZX_OK) {
          DISP_ERROR("Error reading MIPI register 0x%x (%d)", buffer[i + 2], status);
          return status;
        }
        break;
      case kDsiOpPhyPowerOn:
        DISP_TRACE("dsi_phy_power_on size=%d", payload_size);
        set_signal_power_(/*on=*/true);
        break;
      case kDsiOpPhyPowerOff:
        DISP_TRACE("dsi_phy_power_off size=%d", payload_size);
        set_signal_power_(/*on=*/false);
        break;
      // All other cmd_type bytes are real DSI commands
      case kMipiDsiDtDcsShortWrite0:
      case kMipiDsiDtDcsShortWrite1:
      case /*kMipiDsiDtDcsShortWrite2*/ 0x25:
      case kMipiDsiDtDcsLongWrite:
      case kMipiDsiDtDcsRead0:
        is_dcs = true;
        __FALLTHROUGH;
      default:
        DISP_TRACE("dsi_cmd op=0x%x size=%d is_dcs=%s", cmd_type, payload_size,
                   is_dcs ? "yes" : "no");
        ZX_DEBUG_ASSERT(cmd_type != 0x37);
        // Create the command using mipi-dsi library
        mipi_dsi_cmd_t cmd;
        status =
            mipi_dsi::MipiDsi::CreateCommand(&buffer[i + 2], payload_size, NULL, 0, is_dcs, &cmd);
        if (status == ZX_OK) {
          if ((status = dsiimpl_.SendCmd(&cmd, 1)) != ZX_OK) {
            DISP_ERROR("Error loading LCD init table. Aborting %d\n", status);
            return status;
          }
        } else {
          DISP_ERROR("Invalid command at byte 0x%lx (%d). Skipping\n", i, status);
        }
        break;
    }
    // increment by payload length
    i += payload_size + kMinCmdSize;
  }
  return status;
}

zx_status_t Lcd::Disable() {
  if (!enabled_) {
    DISP_INFO("LCD is already off, no work to do");
    return ZX_OK;
  }
  if (dsi_off_.size() == 0) {
    DISP_ERROR("Unsupported panel (%d) detected!", panel_type_);
    return ZX_ERR_NOT_SUPPORTED;
  }
  DISP_INFO("Powering off the LCD [type=%d]", panel_type_);
  auto status = LoadInitTable(dsi_off_);
  if (status != ZX_OK) {
    DISP_ERROR("Failed to execute panel off sequence (%d)", status);
    return status;
  }
  enabled_ = false;
  return ZX_OK;
}

zx_status_t Lcd::Enable() {
  if (enabled_) {
    DISP_INFO("LCD is already on, no work to do");
    return ZX_OK;
  }

  if (dsi_on_.size() == 0) {
    DISP_ERROR("Unsupported panel (%d) detected!", panel_type_);
    return ZX_ERR_NOT_SUPPORTED;
  }

  DISP_INFO("Powering on the LCD [type=%d]", panel_type_);
  auto status = LoadInitTable(dsi_on_);
  if (status != ZX_OK) {
    DISP_ERROR("Failed to execute panel init sequence (%d)", status);
    return status;
  }

  // check status
  if (GetDisplayId() != ZX_OK) {
    DISP_ERROR("Cannot communicate with LCD Panel!\n");
    return ZX_ERR_TIMED_OUT;
  }
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  // LCD is on now.
  enabled_ = true;
  return ZX_OK;
}

zx::result<Lcd*> Lcd::Create(fbl::AllocChecker* ac, uint32_t panel_type,
                             cpp20::span<const uint8_t> dsi_on, cpp20::span<const uint8_t> dsi_off,
                             fit::function<void(bool)> set_signal_power,
                             ddk::DsiImplProtocolClient dsiimpl, ddk::GpioProtocolClient gpio,
                             bool already_enabled) {
  std::unique_ptr<Lcd> lcd =
      fbl::make_unique_checked<Lcd>(ac, panel_type, std::move(set_signal_power));
  lcd->dsi_on_ = dsi_on;
  lcd->dsi_off_ = dsi_off;
  lcd->dsiimpl_ = dsiimpl;

  if (!gpio.is_valid()) {
    DISP_ERROR("Could not obtain GPIO protocol\n");
    return zx::error(ZX_ERR_NO_RESOURCES);
  }
  lcd->gpio_ = gpio;

  lcd->enabled_ = already_enabled;
  if (already_enabled) {
    DISP_INFO("LCD Enabled by Bootloader. Skipping panel init\n");
  } else {
    lcd->Enable();
  }

  return zx::ok(lcd.release());
}

}  // namespace amlogic_display
