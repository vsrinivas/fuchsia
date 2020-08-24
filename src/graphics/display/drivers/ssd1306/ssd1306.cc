// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/ssd1306/ssd1306.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/i2c.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>

namespace ssd1306 {

zx_status_t Ssd1306::FidlGetConfig(void* ctx, fidl_txn_t* txn) {
  fuchsia_hardware_dotmatrixdisplay_DotmatrixDisplayConfig config = {};
  config.width = kDisplayWidth;
  config.height = kDisplayHeight;
  config.format = fuchsia_hardware_dotmatrixdisplay_PixelFormat_MONOCHROME;
  config.layout = fuchsia_hardware_dotmatrixdisplay_ScreenLayout_COLUMN_TB_ROW_LR;

  return fuchsia_hardware_dotmatrixdisplay_DotmatrixDisplayGetConfig_reply(txn, &config);
}

zx_status_t Ssd1306::FidlSetScreen(void* ctx, const uint8_t* screen_buffer_list,
                                   size_t screen_buffer_count, fidl_txn* txn) {
  Ssd1306* ssd = reinterpret_cast<Ssd1306*>(ctx);
  zx_status_t status = ssd->DotmatrixDisplaySetScreen(screen_buffer_list, screen_buffer_count);
  return fuchsia_hardware_dotmatrixdisplay_DotmatrixDisplaySetScreen_reply(txn, status);
}

zx_status_t Ssd1306::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  static const fuchsia_hardware_dotmatrixdisplay_DotmatrixDisplay_ops_t kOps = {
      .GetConfig = Ssd1306::FidlGetConfig,
      .SetScreen = Ssd1306::FidlSetScreen,
  };
  return fuchsia_hardware_dotmatrixdisplay_DotmatrixDisplay_dispatch(this, txn, msg, &kOps);
}

static zx_status_t WriteCommand(ddk::I2cChannel& i2c, uint8_t reg_address, uint8_t data) {
  uint8_t send[2] = {reg_address, data};
  return i2c.WriteSync(send, static_cast<uint8_t>(sizeof(send)));
}

void Ssd1306::DotmatrixDisplayGetConfig(dotmatrix_display_config_t* out_config) {
  out_config->width = kDisplayWidth;
  out_config->height = kDisplayHeight;
  out_config->format = PIXEL_FORMAT_MONOCHROME;
  out_config->layout = SCREEN_LAYOUT_COLUMN_TB_ROW_LR;
}

zx_status_t Ssd1306::DotmatrixDisplaySetScreen(const uint8_t* screen_buffer_list,
                                               size_t screen_buffer_count) {
  if (screen_buffer_count > (kDisplayWidth * kDisplayHeight / 8)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  size_t row = 0;
  size_t col = 0;
  for (size_t i = 0; i < screen_buffer_count; i++) {
    frame_buffer_[row][col] = screen_buffer_list[i];
    col++;
    if (col == kDisplayWidth) {
      col = 0;
      row++;
    }
  }
  return FlushScreen();
}

zx_status_t Ssd1306::FlushScreen() {
  if (!is_enabled_) {
    return ZX_ERR_SHOULD_WAIT;
  }
  for (int i = 0; i < (kDisplayHeight / 8); i++) {
    zx_status_t status = WriteCommand(i2c_, 0x00, static_cast<uint8_t>(0xB0 + i));
    if (status != ZX_OK) {
      return status;
    }
    status = WriteCommand(i2c_, 0x00, 0x00);
    if (status != ZX_OK) {
      return status;
    }
    status = WriteCommand(i2c_, 0x00, 0x10);
    if (status != ZX_OK) {
      return status;
    }

    uint8_t v[kDisplayWidth + 1];
    v[0] = kI2cFbAddress;
    for (size_t j = 1; j < countof(v); j++) {
      v[j] = frame_buffer_[i][j - 1];
    }
    status = i2c_.WriteSync(v, sizeof(v));
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t Ssd1306::EnableScreen() {
  zx_status_t status;
  for (size_t i = 0; i < countof(kPowerOnSequence); i++) {
    status = WriteCommand(i2c_, 0x00, kPowerOnSequence[i]);
    if (status != ZX_OK) {
      return status;
    }
  }
  // Set screen to solid on-color.
  for (int i = 0; i < (kDisplayHeight / 8); i++) {
    for (size_t j = 0; j < kDisplayWidth; j++) {
      frame_buffer_[i][j] = kDefaultColor;
    }
  }

  is_enabled_ = true;
  status = FlushScreen();
  if (status != ZX_OK) {
    is_enabled_ = false;
    return status;
  }

  return ZX_OK;
}

void Ssd1306::DdkUnbindNew(ddk::UnbindTxn txn) {
  thrd_join(enable_thread_, NULL);
  txn.Reply();
}

zx_status_t Ssd1306::Bind(ddk::I2cChannel i2c) {
  i2c_ = std::move(i2c);

  auto f = [](void* arg) -> int {
    zx_status_t status = reinterpret_cast<Ssd1306*>(arg)->EnableScreen();
    if (status != ZX_OK) {
      return status;
    }
    return 0;
  };
  int rc = thrd_create_with_name(&enable_thread_, f, this, "ssd1306-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  return DdkAdd("ssd1306");
}

zx_status_t ssd1306_bind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<Ssd1306>(&ac, device);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  ddk::I2cChannel i2c(device);
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "I2c-Hid: Could not get i2c protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto status = dev->Bind(std::move(i2c));
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

}  // namespace ssd1306

static zx_driver_ops_t ssd1306_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = ssd1306::ssd1306_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(ssd1306, ssd1306_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_SSD1306),
ZIRCON_DRIVER_END(ssd1306)
    // clang-format on
