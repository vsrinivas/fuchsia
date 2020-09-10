// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_ALC5663_TESTS_FAKE_I2C_H_
#define SRC_MEDIA_AUDIO_DRIVERS_ALC5663_TESTS_FAKE_I2C_H_

#include <endian.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/function.h>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/protocol/i2c.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/i2c.h>
#include <fbl/string_buffer.h>
#include <fbl/string_printf.h>
#include <fbl/vector.h>

namespace audio::alc5663 {

// A fake I2C device.
//
// This class helps users implement fake I2C hardware. In particular,
// hardware which uses the style of having reads and writes of
// fixed-size data words at fixed-size addresses can use this class to
// listen and respond to the reads and writes of an I2C driver.
//
// The constructor takes two callbacks: "on_read" and "on_write" which
// will be invoked each time the driver performs a read or write
// respectively.
template <typename AddressType, typename DataType>
class FakeI2c : ddk::I2cProtocol<FakeI2c<AddressType, DataType>> {
 public:
  // Construct a FakeI2c, which will call the given function "on_read" each time
  // a read takes place to this device, and similarly call "on_write" for writes.
  FakeI2c(fit::function<DataType(AddressType)> on_read,
          fit::function<void(AddressType, DataType)> on_write)
      : ddk::I2cProtocol<FakeI2c>(), on_read_(std::move(on_read)), on_write_(std::move(on_write)) {}

  // Get the protocol ops for this object.
  i2c_protocol_t GetProto() { return {&this->i2c_protocol_ops_, this}; }

  // Implementation of |ddk::I2cProtocol|.
  void I2cTransact(const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
                   void* cookie);
  zx_status_t I2cGetMaxTransferSize(size_t* /*out_size*/) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t I2cGetInterrupt(uint32_t /*flags*/, zx::interrupt* /*out_irq*/) {
    return ZX_ERR_NOT_SUPPORTED;
  }

 private:
  fit::function<DataType(AddressType)> on_read_;
  fit::function<void(AddressType, DataType)> on_write_;
};

// Implementation details follow.

namespace fake_i2c_internal {

// Convert between host and big endian for the I2C bus.
inline uint32_t HostToBigEndian(uint32_t val) { return htobe32(val); }
inline uint16_t HostToBigEndian(uint16_t val) { return htobe16(val); }
inline uint8_t HostToBigEndian(uint8_t val) { return val; }

inline uint32_t BigEndianToHost(uint32_t val) { return betoh32(val); }
inline uint16_t BigEndianToHost(uint16_t val) { return betoh16(val); }
inline uint8_t BigEndianToHost(uint8_t val) { return val; }

}  // namespace fake_i2c_internal

template <typename AddressType, typename DataType>
void FakeI2c<AddressType, DataType>::I2cTransact(const i2c_op_t* op_list, size_t op_count,
                                                 i2c_transact_callback callback, void* cookie) {
  // Is this a read? (represented on the wire as a write of an address followed by a read.)
  if (op_count == 2 && !op_list[0].is_read && op_list[0].data_size == sizeof(AddressType) &&
      op_list[1].is_read && op_list[1].data_size == sizeof(DataType)) {
    // Decode address.
    AddressType addr = (reinterpret_cast<const AddressType*>(op_list[0].data_buffer))[0];

    // Issue callback.
    DataType result =
        fake_i2c_internal::HostToBigEndian(on_read_(fake_i2c_internal::BigEndianToHost(addr)));

    // Return to caller.
    fbl::Vector<i2c_op_t> result_ops = {{
        .data_buffer = &result,
        .data_size = sizeof(result),
        .is_read = false,
        .stop = true,
    }};
    callback(cookie, ZX_OK, result_ops.data(), result_ops.size());
    return;
  }

  // Is this a write? (represented on the wire as a write of an address, followed by a write of
  // data.)
  if (op_count == 1 && !op_list[0].is_read && op_list[0].stop &&
      op_list[0].data_size == sizeof(DataType) + sizeof(AddressType)) {
    // Decode data.
    struct Payload {
      AddressType address;
      DataType data;
    } __PACKED;
    const auto* payload = reinterpret_cast<const Payload*>(op_list[0].data_buffer);

    // Issue callback.
    on_write_(fake_i2c_internal::BigEndianToHost(payload->address),
              fake_i2c_internal::BigEndianToHost(payload->data));

    // Return to caller.
    fbl::Vector<i2c_op_t> empty_result = {};
    callback(cookie, ZX_OK, empty_result.data(), empty_result.size());
    return;
  }

  // Can't handle this: log the transaction, and abort execution.
  zxlogf(ERROR, "Unsupported I2C transation:");
  for (size_t i = 0; i < op_count; i++) {
    if (op_list[i].is_read) {
      zxlogf(ERROR, " * READ of %ld byte(s)", op_list[i].data_size);
    } else {
      fbl::StringBuffer<1024> buff;
      const auto* data = reinterpret_cast<const uint8_t*>(op_list[i].data_buffer);
      for (size_t j = 0; j < op_list[i].data_size; j++) {
        buff.Append(fbl::StringPrintf(" %02x", data[j]));
      }
      zxlogf(ERROR, " * WRITE of %ld byte(s): %s", op_list[i].data_size, buff.c_str());
    }
    if (op_list[i].stop) {
      zxlogf(ERROR, " * STOP");
    }
  }
  ZX_PANIC("Unsupported I2C transaction.");
}

}  // namespace audio::alc5663

#endif  // SRC_MEDIA_AUDIO_DRIVERS_ALC5663_TESTS_FAKE_I2C_H_
