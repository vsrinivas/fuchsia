// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_ALC5663_TESTS_FAKE_I2C_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_ALC5663_TESTS_FAKE_I2C_H_

#include <endian.h>
#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/fit/function.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/string_buffer.h>
#include <fbl/string_printf.h>
#include <fbl/vector.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

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
class FakeI2c : public fidl::WireServer<fuchsia_hardware_i2c::Device> {
 public:
  // Construct a FakeI2c, which will call the given function "on_read" each time
  // a read takes place to this device, and similarly call "on_write" for writes.
  FakeI2c(fit::function<DataType(AddressType)> on_read,
          fit::function<void(AddressType, DataType)> on_write)
      : on_read_(std::move(on_read)), on_write_(std::move(on_write)) {}

  // Implementation of |fuchsia.hardware.i2c.Device|.
  void Transfer(TransferRequestView request, TransferCompleter::Sync& completer) override;

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
void FakeI2c<AddressType, DataType>::Transfer(TransferRequestView request,
                                              TransferCompleter::Sync& completer) {
  const size_t op_count = request->transactions.count();
  const auto* op_list = request->transactions.data();

  // Is this a read? (represented on the wire as a write of an address followed by a read.)
  if (op_count == 2 && op_list[0].has_data_transfer() &&
      op_list[0].data_transfer().is_write_data() &&
      op_list[0].data_transfer().write_data().count() == sizeof(AddressType) &&
      op_list[1].has_data_transfer() && op_list[1].data_transfer().is_read_size() &&
      op_list[1].data_transfer().read_size() == sizeof(DataType)) {
    // Decode address.
    AddressType addr;
    memcpy(&addr, op_list[0].data_transfer().write_data().data(), sizeof(addr));

    // Issue callback.
    DataType result =
        fake_i2c_internal::HostToBigEndian(on_read_(fake_i2c_internal::BigEndianToHost(addr)));

    // Return to caller.
    fidl::Arena arena;
    fidl::ObjectView<fuchsia_hardware_i2c::wire::DeviceTransferResponse> response(arena);
    response->read_data = {arena, 1};
    response->read_data[0] = {arena, sizeof(result)};
    memcpy(response->read_data[0].data(), &result, sizeof(result));
    completer.Reply(::fit::ok(response.get()));
    return;
  }

  // Is this a write? (represented on the wire as a write of an address, followed by a write of
  // data.)
  if (op_count == 1 && op_list[0].has_data_transfer() &&
      op_list[0].data_transfer().is_write_data() &&
      op_list[0].data_transfer().write_data().count()) {
    // Decode data.
    struct Payload {
      AddressType address;
      DataType data;
    } __PACKED;
    Payload payload;
    memcpy(&payload, op_list[0].data_transfer().write_data().data(), sizeof(payload));

    // Issue callback.
    on_write_(fake_i2c_internal::BigEndianToHost(payload.address),
              fake_i2c_internal::BigEndianToHost(payload.data));

    // Return to caller.
    fidl::Arena arena;
    fidl::ObjectView<fuchsia_hardware_i2c::wire::DeviceTransferResponse> response(arena);
    response->read_data = {arena, 0};
    completer.Reply(::fit::ok(response.get()));
    return;
  }

  // Can't handle this: log the transaction, and abort execution.
  zxlogf(ERROR, "Unsupported I2C transation:");
  for (size_t i = 0; i < op_count; i++) {
    if (op_list[i].has_data_transfer() && op_list[i].data_transfer().is_read_size()) {
      zxlogf(ERROR, " * READ of %u byte(s)", op_list[i].data_transfer().read_size());
    } else if (op_list[i].has_data_transfer() && op_list[i].data_transfer().is_write_data()) {
      fbl::StringBuffer<1024> buff;
      const auto* data =
          reinterpret_cast<const uint8_t*>(op_list[i].data_transfer().write_data().data());
      for (size_t j = 0; j < op_list[i].data_transfer().write_data().count(); j++) {
        buff.Append(fbl::StringPrintf(" %02x", data[j]));
      }
      zxlogf(ERROR, " * WRITE of %ld byte(s): %s", op_list[i].data_transfer().write_data().count(),
             buff.c_str());
    } else {
      zxlogf(ERROR, " * (No data transfer)");
    }
    if (op_list[i].has_stop() && op_list[i].stop()) {
      zxlogf(ERROR, " * STOP");
    }
  }
  ZX_PANIC("Unsupported I2C transaction.");
}

}  // namespace audio::alc5663

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_ALC5663_TESTS_FAKE_I2C_H_
