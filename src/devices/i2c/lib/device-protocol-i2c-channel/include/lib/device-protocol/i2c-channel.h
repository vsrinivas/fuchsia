// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_I2C_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_INCLUDE_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_H_
#define SRC_DEVICES_I2C_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_INCLUDE_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_H_

#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <fuchsia/hardware/i2c/cpp/banjo.h>
#include <lib/device-protocol/i2c.h>
#include <lib/sync/completion.h>
#include <zircon/types.h>

namespace ddk {

// TODO(fxbug.dev/96293): Merge I2cFidlChannel back into I2cChannel and delete I2cChannelBase once
// all clients are using FIDL.
class I2cChannelBase {
 public:
  struct StatusRetries {
    zx_status_t status;
    uint8_t retries;
  };

  virtual ~I2cChannelBase() = default;

  // Performs typical i2c Read: writes device register address (1 byte) followed
  // by len reads into buf.
  zx_status_t ReadSync(uint8_t addr, uint8_t* buf, size_t len) {
    return WriteReadSync(&addr, 1, buf, len);
  }

  // Writes len bytes from buffer with no trailing read
  zx_status_t WriteSync(const uint8_t* buf, size_t len) {
    return WriteReadSync(buf, len, nullptr, 0);
  }

  virtual zx_status_t WriteReadSync(const uint8_t* tx_buf, size_t tx_len, uint8_t* rx_buf,
                                    size_t rx_len) = 0;

  // ReadSync with retries, returns status and retry attempts.
  StatusRetries ReadSyncRetries(uint8_t addr, uint8_t* buf, size_t len, uint8_t retries,
                                zx::duration delay) {
    return WriteReadSyncRetries(&addr, 1, buf, len, retries, delay);
  }

  // WriteSync with retries, returns status and retry attempts.
  StatusRetries WriteSyncRetries(const uint8_t* buf, size_t len, uint8_t retries,
                                 zx::duration delay) {
    return WriteReadSyncRetries(buf, len, nullptr, 0, retries, delay);
  }

  // WriteReadSync with retries, returns status and retry attempts.
  StatusRetries WriteReadSyncRetries(const uint8_t* tx_buf, size_t tx_len, uint8_t* rx_buf,
                                     size_t rx_len, uint8_t retries, zx::duration delay) {
    uint8_t attempt = 0;
    auto status = WriteReadSync(tx_buf, tx_len, rx_buf, rx_len);
    while (status != ZX_OK && attempt < retries) {
      zx::nanosleep(zx::deadline_after(delay));
      attempt++;
      status = WriteReadSync(tx_buf, tx_len, rx_buf, rx_len);
    }
    return {status, attempt};
  }
};

class I2cChannel : public I2cChannelBase, public I2cProtocolClient {
 public:
  I2cChannel() = default;

  I2cChannel(const i2c_protocol_t* proto) : I2cProtocolClient(proto) {}

  I2cChannel(zx_device_t* parent) : I2cProtocolClient(parent) {}

  I2cChannel(zx_device_t* parent, const char* fragment_name)
      : I2cProtocolClient(parent, fragment_name) {}

  ~I2cChannel() override = default;

  zx_status_t WriteReadSync(const uint8_t* tx_buf, size_t tx_len, uint8_t* rx_buf,
                            size_t rx_len) override {
    i2c_protocol_t proto;
    GetProto(&proto);
    return i2c_write_read_sync(&proto, tx_buf, tx_len, rx_buf, rx_len);
  }
};

class I2cFidlChannel : public I2cChannelBase {
 public:
  explicit I2cFidlChannel(fidl::ClientEnd<fuchsia_hardware_i2c::Device2> client_end)
      : fidl_client_(std::move(client_end)) {}

  ~I2cFidlChannel() override = default;

  fidl::WireResult<fuchsia_hardware_i2c::Device2::Transfer> Transfer(
      fidl::VectorView<bool> segments_is_write,
      fidl::VectorView<fidl::VectorView<uint8_t>> write_segments_data,
      fidl::VectorView<uint32_t> read_segments_length) {
    return fidl_client_->Transfer(segments_is_write, write_segments_data, read_segments_length);
  }

  zx_status_t WriteReadSync(const uint8_t* tx_buf, size_t tx_len, uint8_t* rx_buf,
                            size_t rx_len) override {
    if (tx_len > fuchsia_hardware_i2c::wire::kMaxTransferSize ||
        rx_len > fuchsia_hardware_i2c::wire::kMaxTransferSize) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    fidl::Arena arena;

    fidl::VectorView<bool> segments_is_write;
    fidl::VectorView<fidl::VectorView<uint8_t>> write_segments;
    fidl::VectorView<uint32_t> read_segments_length;

    if (tx_len > 0 && rx_len > 0) {
      segments_is_write = fidl::VectorView<bool>(arena, 2);
      segments_is_write[0] = true;
      segments_is_write[1] = false;
    } else if (tx_len > 0) {
      segments_is_write = fidl::VectorView<bool>(arena, 1);
      segments_is_write[0] = true;
    } else if (rx_len > 0) {
      segments_is_write = fidl::VectorView<bool>(arena, 1);
      segments_is_write[0] = false;
    } else {
      return ZX_OK;
    }

    if (tx_len > 0) {
      write_segments = fidl::VectorView<fidl::VectorView<uint8_t>>(arena, 1);
      write_segments[0] = fidl::VectorView<uint8_t>(arena, tx_len);
      memcpy(write_segments[0].mutable_data(), tx_buf, tx_len);
    }

    if (rx_len > 0) {
      read_segments_length = fidl::VectorView<uint32_t>(arena, 1);
      read_segments_length[0] = static_cast<uint32_t>(rx_len);
    }

    const auto reply =
        fidl_client_->Transfer(segments_is_write, write_segments, read_segments_length);
    if (!reply.ok()) {
      return reply.status();
    }
    if (reply->result.is_err()) {
      return reply->result.err();
    }

    if (rx_len > 0) {
      const auto& read_segments_data = reply->result.response().read_segments_data;
      if (read_segments_data.count() != 1 || read_segments_data[0].count() != rx_len) {
        return ZX_ERR_IO;
      }

      memcpy(rx_buf, read_segments_data[0].data(), rx_len);
    }

    return ZX_OK;
  }

 private:
  fidl::WireSyncClient<fuchsia_hardware_i2c::Device2> fidl_client_;
};

}  // namespace ddk

#endif  // SRC_DEVICES_I2C_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_INCLUDE_LIB_DEVICE_PROTOCOL_I2C_CHANNEL_H_
