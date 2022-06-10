// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <fuchsia/hardware/i2c/cpp/banjo.h>
#include <lib/device-protocol/i2c.h>
#include <lib/zx/interrupt.h>

#ifndef SRC_DEVICES_I2C_TESTING_FAKE_I2C_INCLUDE_LIB_FAKE_I2C_FAKE_I2C_H_
#define SRC_DEVICES_I2C_TESTING_FAKE_I2C_INCLUDE_LIB_FAKE_I2C_FAKE_I2C_H_

namespace fake_i2c {

// This class fakes an I2c device by implementing the I2cProtocol. When a tester wants to
// have a fake I2c device, they should create a class that inherents FakeI2c and implements
// the |Transact| function. The |Transact| function is where the Fake driver can view the I2C
// Write data and return Read data.
//
// The FakeI2c class is helpful because it serializes the Read and Write data, making it easier
// for a tester to write a fake.
//
// Here's an example:
//
// class FakeLightSensor : public FakeI2c {
//   virtual zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size,
//                              uint8_t* read_buffer, size_t* read_buffer_size) {
//      if (CompareWrite(write_buffer, write_buffer_size, kReadLightRegisterCommand,
//                       sizeof(kReadLightRegisterCommand)) {
//        SetRead(light_sensor_data_, sizeof(light_sensor_data_), read_buffer,
//                sizeof(read_buffer_size);
//        return ZX_OK;
//      }
//      return ZX_ERR_NOT_SUPPORTED;
//   }
// }
//
class FakeI2c : public ddk::I2cProtocol<FakeI2c>,
                public fidl::WireServer<fuchsia_hardware_i2c::Device> {
 public:
  FakeI2c() : proto_({&i2c_protocol_ops_, this}) {}

  // This function takes the |op_list| and serialies the write data so it is easier to use
  // in a fake. This will call |Transact| with the serialzed data.
  void I2cTransact(const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
                   void* cookie) {
    zx_status_t status;

    // Serialize the write information.
    uint8_t write_buffer[I2C_MAX_TOTAL_TRANSFER];
    size_t write_buffer_index = 0;
    for (size_t i = 0; i < op_count; ++i) {
      if (!op_list[i].is_read) {
        if (write_buffer_index + op_list[i].data_size >= I2C_MAX_TOTAL_TRANSFER) {
          callback(cookie, ZX_ERR_NO_MEMORY, nullptr, 0);
          return;
        }
        memcpy(write_buffer + write_buffer_index, op_list[i].data_buffer, op_list[i].data_size);
        write_buffer_index += op_list[i].data_size;
      }
    }

    // Process the serialized ops.
    uint8_t read_buffer[I2C_MAX_TOTAL_TRANSFER];
    size_t read_buffer_size = 0;
    status = Transact(write_buffer, write_buffer_index, read_buffer, &read_buffer_size);
    if (status != ZX_OK) {
      callback(cookie, status, nullptr, 0);
      return;
    }

    // Return a read op if we have one.
    if (read_buffer_size > 0) {
      i2c_op_t read_op = {};
      read_op.is_read = true;
      read_op.stop = true;
      read_op.data_buffer = read_buffer;
      read_op.data_size = read_buffer_size;
      callback(cookie, ZX_OK, &read_op, 1);
    } else {
      callback(cookie, ZX_OK, nullptr, 0);
    }
  }

  zx_status_t I2cGetMaxTransferSize(size_t* out_size) {
    *out_size = I2C_MAX_TOTAL_TRANSFER;
    return ZX_OK;
  }

  zx_status_t I2cGetInterrupt(uint32_t flags, zx::interrupt* out_irq) {
    return irq_.duplicate(ZX_RIGHT_SAME_RIGHTS, out_irq);
  }

  void SetInterrupt(zx::interrupt irq) { irq_ = std::move(irq); }

  i2c_protocol_t* GetProto() { return &proto_; }

  void Transfer(TransferRequestView request, TransferCompleter::Sync& completer) override {
    // Serialize the write information.
    uint8_t write_buffer[I2C_MAX_TOTAL_TRANSFER];
    size_t write_buffer_index = 0;
    for (const auto& transaction : request->transactions) {
      if (!transaction.has_data_transfer()) {
        completer.ReplyError(ZX_ERR_INVALID_ARGS);
        return;
      }

      if (transaction.data_transfer().is_write_data()) {
        const auto& write_data = transaction.data_transfer().write_data();
        if (write_buffer_index + write_data.count() > I2C_MAX_TOTAL_TRANSFER) {
          completer.ReplyError(ZX_ERR_NO_MEMORY);
          return;
        }
        memcpy(write_buffer + write_buffer_index, write_data.data(), write_data.count());
        write_buffer_index += write_data.count();
      }
    }

    // Process the serialized ops.
    uint8_t read_buffer[I2C_MAX_TOTAL_TRANSFER];
    size_t read_buffer_size = 0;
    zx_status_t status = Transact(write_buffer, write_buffer_index, read_buffer, &read_buffer_size);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }

    fidl::Arena arena;
    fidl::ObjectView<fuchsia_hardware_i2c::wire::DeviceTransferResponse> response(arena);

    if (read_buffer_size > 0) {
      response->read_data = {arena, 1};
      response->read_data[0] = {arena, read_buffer_size};
      memcpy(response->read_data[0].mutable_data(), read_buffer, read_buffer_size);
    }

    completer.Reply(::fitx::ok(response.get()));
  }

 protected:
  // The main function to be overriden for a specific fake. This is called on each
  // I2cTransact, but with serialized write and read information so it is easier to
  // use.
  virtual zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size,
                               uint8_t* read_buffer, size_t* read_buffer_size) = 0;

  // Helper functions for specific fakes to use inside of |Transact|.
  bool CompareWrite(const uint8_t* write_buffer, size_t write_buffer_size,
                    const uint8_t* command_buffer, size_t command_buffer_size) {
    return (write_buffer_size == command_buffer_size) &&
           (memcmp(write_buffer, command_buffer, write_buffer_size) == 0);
  }

  void SetRead(const void* return_buffer, size_t return_buffer_size, uint8_t* read_buffer,
               size_t* read_buffer_size) {
    size_t read_size =
        (return_buffer_size < I2C_MAX_TOTAL_TRANSFER) ? return_buffer_size : I2C_MAX_TOTAL_TRANSFER;
    memcpy(read_buffer, return_buffer, read_size);
    *read_buffer_size = read_size;
  }

  zx::interrupt irq_;
  i2c_protocol_t proto_;
};

}  // namespace fake_i2c

#endif  // SRC_DEVICES_I2C_TESTING_FAKE_I2C_INCLUDE_LIB_FAKE_I2C_FAKE_I2C_H_
