// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/ssd1306/ssd1306.h"

#include <fuchsia/hardware/i2c/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>

#include <fbl/alloc_checker.h>

#include "src/graphics/display/drivers/ssd1306/ssd1306-bind.h"

namespace ssd1306 {

void Ssd1306::GetConfig(GetConfigRequestView request, GetConfigCompleter::Sync& completer) {
  fuchsia_hardware_dotmatrixdisplay::wire::DotmatrixDisplayConfig config;
  config.width = kDisplayWidth;
  config.height = kDisplayHeight;
  config.format = fuchsia_hardware_dotmatrixdisplay::wire::PixelFormat::kMonochrome;
  config.layout = fuchsia_hardware_dotmatrixdisplay::wire::ScreenLayout::kColumnTbRowLr;

  completer.Reply(config);
}

void Ssd1306::SetScreen(SetScreenRequestView request, SetScreenCompleter::Sync& completer) {
  zx_status_t status =
      DotmatrixDisplaySetScreen(request->screen_buffer.data(), request->screen_buffer.count());
  completer.Reply(status);
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
    for (size_t j = 1; j < std::size(v); j++) {
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
  for (size_t i = 0; i < std::size(kPowerOnSequence); i++) {
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

void Ssd1306::DdkUnbind(ddk::UnbindTxn txn) {
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

ZIRCON_DRIVER(ssd1306, ssd1306_driver_ops, "zircon", "0.1");
