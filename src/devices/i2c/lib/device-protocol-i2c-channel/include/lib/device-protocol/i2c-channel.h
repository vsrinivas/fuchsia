// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_I2C_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_INCLUDE_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_H_
#define SRC_DEVICES_I2C_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_INCLUDE_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_H_

#include <lib/device-protocol/i2c.h>
#include <lib/sync/completion.h>
#include <zircon/types.h>

#include <ddktl/protocol/i2c.h>

namespace ddk {

class I2cChannel : public I2cProtocolClient {
 public:
  I2cChannel() {}

  I2cChannel(const i2c_protocol_t* proto) : I2cProtocolClient(proto) {}

  I2cChannel(zx_device_t* parent) : I2cProtocolClient(parent) {}

  ~I2cChannel() = default;

  // Performs typical i2c Read: writes device register address (1 byte) followed
  // by len reads into buf.
  zx_status_t ReadSync(uint8_t addr, uint8_t* buf, size_t len) {
    return WriteReadSync(&addr, 1, buf, len);
  }

  // Writes len bytes from buffer with no trailing read
  zx_status_t WriteSync(const uint8_t* buf, size_t len) {
    return WriteReadSync(buf, len, nullptr, 0);
  }

  zx_status_t WriteReadSync(const uint8_t* tx_buf, size_t tx_len, uint8_t* rx_buf, size_t rx_len) {
    i2c_protocol_t proto;
    GetProto(&proto);
    return i2c_write_read_sync(&proto, tx_buf, tx_len, rx_buf, rx_len);
  }
};

}  // namespace ddk

#endif  // SRC_DEVICES_I2C_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_INCLUDE_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_H_
