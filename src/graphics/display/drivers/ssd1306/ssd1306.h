// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/dotmatrixdisplay/c/fidl.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/device-protocol/i2c.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>

#include <array>

#include <ddktl/device.h>
#include <ddktl/protocol/dotmatrixdisplay.h>
#include <ddktl/protocol/i2c.h>

namespace ssd1306 {

class Ssd1306;
using DeviceType = ddk::Device<Ssd1306, ddk::Unbindable, ddk::Messageable>;
class Ssd1306 : public DeviceType,
                public ddk::DotmatrixDisplayProtocol<Ssd1306, ddk::base_protocol> {
 public:
  Ssd1306(zx_device_t* parent) : DeviceType(parent), frame_buffer_(), i2c_(parent) {}

  zx_status_t Bind(ddk::I2cChannel i2c);
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease() { delete this; }

  void DotmatrixDisplayGetConfig(dotmatrix_display_config_t* out_config);
  zx_status_t DotmatrixDisplaySetScreen(const uint8_t* screen_buffer_list,
                                        size_t screen_buffer_count);

  static zx_status_t FidlGetConfig(void* ctx, fidl_txn_t* txn);
  static zx_status_t FidlSetScreen(void* ctx, const uint8_t* screen_buffer_list,
                                   size_t screen_buffer_count, fidl_txn* txn);

  zx_status_t FlushScreen();

 private:
  static constexpr int kDefaultColor = 0xFF;

  static constexpr int kDisplayWidth = 128;
  static_assert((kDisplayWidth % 8 == 0));

  static constexpr int kDisplayHeight = 64;
  static_assert((kDisplayHeight % 8 == 0));

  static constexpr int kI2cFbAddress = 0x40;
  static constexpr uint8_t kPowerOnSequence[] = {
      0xAE, 0x00, 0x10, 0x40, 0xB0, 0x81, 0xCF, 0xA1, 0xA6, 0xA8, 0x3F, 0xC8, 0xD3,
      0x00, 0xD5, 0x80, 0xD9, 0xF1, 0xDA, 0x12, 0xDB, 0x40, 0x8D, 0x14, 0xAF, 0xAF,
  };

  zx_status_t EnableScreen();

  thrd_t enable_thread_;
  bool is_enabled_ = false;
  std::array<std::array<uint8_t, kDisplayWidth>, kDisplayHeight / 8> frame_buffer_;
  ddk::I2cChannel i2c_;
};

}  // namespace ssd1306
