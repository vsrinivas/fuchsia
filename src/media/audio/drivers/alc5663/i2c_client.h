// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_CODEC_ALC5663_I2C_CLIENT_H_
#define ZIRCON_SYSTEM_DEV_CODEC_ALC5663_I2C_CLIENT_H_

#include <ddk/debug.h>
#include <ddktl/protocol/i2c.h>
#include <hwreg/bitfields.h>
#include <lib/device-protocol/i2c-channel.h>
#include <sys/types.h>
#include <zircon/status.h>

#include <type_traits>

namespace audio::alc5663 {

namespace internal {

// Convert between host and big endian for the I2C bus.
inline uint32_t HostToBigEndian(uint32_t val) { return htobe32(val); }
inline uint16_t HostToBigEndian(uint16_t val) { return htobe16(val); }
inline uint8_t HostToBigEndian(uint8_t val) { return val; }

inline uint32_t BigEndianToHost(uint32_t val) { return betoh32(val); }
inline uint16_t BigEndianToHost(uint16_t val) { return betoh16(val); }
inline uint8_t BigEndianToHost(uint8_t val) { return val; }

}  // namespace internal

// Read and write to a device behind a I2cChannel.
//
// We assume the underlying hardware device uses a protocol where:
//
//   * "Read" is implemented by writing the target address register,
//     followed by reading a fixed width data word;
//
//   * "Write" is implemented by writing the target address register
//     immediately followed by a fixed width data word.
//
// These assumptions allow us to provide a simpler interface than the
// raw I2cChannel interface (which supports arbitrary length data
// transfers).
template <typename AddressIntType>
class I2cClient {
 public:
  using AddressType = AddressIntType;

  // Create an I2cClient backed by the given channel.
  explicit I2cClient(ddk::I2cChannel channel) : channel_(channel) {}

  template <typename ValueType>
  zx_status_t Read(AddressIntType addr, ValueType* result);

  template <typename ValueType>
  zx_status_t Write(AddressIntType addr, ValueType val);

 private:
  ddk::I2cChannel channel_;
};

// Perform operations on a Register class.
//
// The type |Register| must be a type with:
//
//   * A public field |data|, containing the raw data to read/write.
//
//   * A public, static constant |kAddress|, dictating the address to
//     write the word two.
//
// For example, the following class implements these operations:
//
//   struct MyRegister {
//     uint16_t data;
//
//     DEF_SUBFIELD(data, 1, 3, some_data);
//     DEF_SUBBIT(data, 16, some_bit);
//
//     static constexpr uint8_t kAddress = 0x18;
//   };

// Read the given register.
template <typename Register, typename AddressType>
[[nodiscard]] zx_status_t ReadRegister(I2cClient<AddressType>* client, Register* result) {
  return client->Read(Register::kAddress, &result->data);
}

// Write the given register.
template <typename Register, typename AddressType>
[[nodiscard]] zx_status_t WriteRegister(I2cClient<AddressType>* client, const Register& value) {
  return client->Write(Register::kAddress, value.data);
}

// Read the given register, pass it through the given function, and then
// write the result back.
//
// If an error occurs in either read or write, we return the status back
// to the user, though provide no way to distinguish which operation
// failed. If this matters, call |ReadRegister| and |WriteRegister|
// directly instead.
template <typename Register, typename AddressType, typename F>
[[nodiscard]] zx_status_t MapRegister(I2cClient<AddressType>* client, F map) {
  Register result{};
  zx_status_t status = client->Read(Register::kAddress, &result.data);
  if (status != ZX_OK) {
    return status;
  }
  result = map(result);
  return client->Write(Register::kAddress, result.data);
}

// Implementation details follow.

template <typename AddressIntType>
template <typename ValueType>
zx_status_t I2cClient<AddressIntType>::Read(AddressIntType addr, ValueType* result) {
  AddressIntType register_addr = internal::HostToBigEndian(static_cast<AddressIntType>(addr));
  ValueType val;
  zx_status_t status =
      channel_.WriteReadSync(reinterpret_cast<uint8_t*>(&register_addr), sizeof(register_addr),
                             reinterpret_cast<uint8_t*>(&val), sizeof(val));
  if (status != ZX_OK) {
    return status;
  }
  *result = internal::BigEndianToHost(val);
  return ZX_OK;
}

template <typename AddressIntType>
template <typename ValueType>
zx_status_t I2cClient<AddressIntType>::Write(AddressIntType addr, ValueType val) {
  struct Buffer {
    AddressIntType register_addr;
    ValueType payload;
  } __PACKED buffer;
  buffer.register_addr = internal::HostToBigEndian(static_cast<AddressIntType>(addr));
  buffer.payload = internal::HostToBigEndian(val);
  return channel_.WriteReadSync(reinterpret_cast<uint8_t*>(&buffer), sizeof(buffer), nullptr, 0);
}

}  // namespace audio::alc5663

#endif  // ZIRCON_SYSTEM_DEV_CODEC_ALC5663_I2C_CLIENT_H_
