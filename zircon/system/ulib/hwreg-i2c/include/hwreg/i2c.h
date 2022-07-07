// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HWREG_I2C_H_
#define HWREG_I2C_H_

#include <endian.h>
#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <zircon/types.h>

#include <hwreg/bitfields.h>

namespace hwreg {

struct LittleEndian;
struct BigEndian;
// An instance of I2cRegisterBase represents a staging copy of a register,
// which can be written to the device's register using i2c protocol. It knows the register's
// address and stores a value for the register. The actual write/read is done upon calling
// ReadFrom()/WriteTo() methods.
//
// All usage rules of RegisterBase applies here with the following exceptions
// - AddrIntSize should match the exact number of bytes used for register addressing.
// - ReadFrom()/WriteTo() methods will return zx_status_t instead of the class object and hence
//   cannot be used in method chaining.
template <class DerivedType, class IntType, size_t AddrIntSize, class ByteOrder = void,
          class PrinterState = void>
class I2cRegisterBase : public RegisterBase<DerivedType, IntType, PrinterState> {
  // Register address size limited to reg_addr() type
  static_assert(AddrIntSize <= sizeof(uint32_t), "unsupported register address width");
  static_assert(std::is_same<ByteOrder, void>::value ||
                    std::is_same<ByteOrder, LittleEndian>::value ||
                    std::is_same<ByteOrder, BigEndian>::value,
                "unsupported byte order");
  // Byte order must be specified if register address/value is more than one byte
  static_assert(!((AddrIntSize > 1 || sizeof(IntType) > 1) && std::is_same<ByteOrder, void>::value),
                "Byte order must be specified");

 public:
  using I2cByteOrder = ByteOrder;
  static constexpr int AddrSize = AddrIntSize;
  // Delete base class ReadFrom() and WriteTo() methods and define new ones
  // which return zx_status_t in case of i2c read/write failure
  template <typename T>
  DerivedType& ReadFrom(T* reg_io) = delete;
  template <typename T>
  DerivedType& WriteTo(T* mmio) = delete;

  using RegisterBaseType = RegisterBase<DerivedType, IntType, PrinterState>;

  zx_status_t ReadFrom(const fidl::ClientEnd<fuchsia_hardware_i2c::Device>& client) {
    uint32_t addr = RegisterBaseType::reg_addr();

    addr = ConvertToI2cByteOrder(addr, AddrIntSize);

    auto* buf = reinterpret_cast<uint8_t*>(&addr);

    fidl::Arena arena;
    fidl::VectorView<fuchsia_hardware_i2c::wire::Transaction> transactions(arena, 2);
    transactions[0] = fuchsia_hardware_i2c::wire::Transaction::Builder(arena)
                          .data_transfer(fuchsia_hardware_i2c::wire::DataTransfer::WithWriteData(
                              arena, fidl::VectorView<uint8_t>::FromExternal(buf, AddrIntSize)))
                          .Build();
    transactions[1] =
        fuchsia_hardware_i2c::wire::Transaction::Builder(arena)
            .data_transfer(fuchsia_hardware_i2c::wire::DataTransfer::WithReadSize(sizeof(IntType)))
            .Build();

    auto response = fidl::WireCall(client)->Transfer(transactions);
    if (!response.ok()) {
      return response.status();
    }
    if (response.value().is_error()) {
      return response.value().error_value();
    }

    if (response.value().value()->read_data.count() != 1) {
      return ZX_ERR_BAD_STATE;
    }
    if (response.value().value()->read_data[0].count() != sizeof(IntType)) {
      return ZX_ERR_BAD_STATE;
    }

    IntType value;
    memcpy(&value, response.value().value()->read_data[0].data(), sizeof(value));
    value = ConvertFromI2cByteOrder(value);
    RegisterBaseType::set_reg_value(value);
    return ZX_OK;
  }

  zx_status_t WriteTo(const fidl::ClientEnd<fuchsia_hardware_i2c::Device>& client) {
    uint32_t addr = RegisterBaseType::reg_addr();
    IntType value = RegisterBaseType::reg_value();

    addr = ConvertToI2cByteOrder(addr, AddrIntSize);
    value = ConvertToI2cByteOrder(value, sizeof(IntType));

    uint8_t buf[AddrIntSize + sizeof(IntType)] = {};
    std::memcpy(buf, &addr, AddrIntSize);
    std::memcpy(buf + AddrIntSize, &value, sizeof(IntType));

    auto write_data = fidl::VectorView<uint8_t>::FromExternal(buf, AddrIntSize + sizeof(IntType));

    fidl::Arena arena;
    fidl::VectorView<fuchsia_hardware_i2c::wire::Transaction> transactions(arena, 1);
    transactions[0] = fuchsia_hardware_i2c::wire::Transaction::Builder(arena)
                          .data_transfer(fuchsia_hardware_i2c::wire::DataTransfer::WithWriteData(
                              arena, write_data))
                          .Build();

    auto response = fidl::WireCall(client)->Transfer(transactions);
    if (!response.ok()) {
      return response.status();
    }
    if (response.value().is_error()) {
      return response.value().error_value();
    }
    return ZX_OK;
  }

 private:
  // Convert 'bytes' number of bytes in value to I2c byte order.
  // 'bytes' is neccesary as reg_addr is uint32_t irrespective of 'AddrIntSize'.
  template <typename T>
  static T ConvertToI2cByteOrder(T value, const size_t bytes) {
    ZX_DEBUG_ASSERT(sizeof(T) >= bytes);

    if (std::is_same<ByteOrder, BigEndian>::value) {
      // Big Endian
      T output = HostToBigEndian(value);
      // Shift right to flush extra MSB i.e. keep only 'AddrIntSize' bytes
      output = static_cast<T>(output >> (sizeof(T) - bytes) * 8);
      return output;
    } else {
      // Little Endian / default (for 1 byte)
      return HostToLittleEndian(value);
    }
  }

  // Convert from I2c Byte order to host.
  template <typename T>
  static T ConvertFromI2cByteOrder(T value) {
    if (std::is_same<ByteOrder, BigEndian>::value) {
      // Big Endian
      return BigEndianToHost(value);
    } else {
      // Little Endian / default (for 1 byte)
      return LittleEndianToHost(value);
    }
  }

  // Methods to convert between host and big/little endian for the I2C bus.
  static inline uint32_t HostToBigEndian(uint32_t val) { return htobe32(val); }
  static inline uint16_t HostToBigEndian(uint16_t val) { return htobe16(val); }
  static inline uint8_t HostToBigEndian(uint8_t val) { return val; }
  static inline uint32_t BigEndianToHost(uint32_t val) { return betoh32(val); }
  static inline uint16_t BigEndianToHost(uint16_t val) { return betoh16(val); }
  static inline uint8_t BigEndianToHost(uint8_t val) { return val; }
  static inline uint32_t HostToLittleEndian(uint32_t val) { return htole32(val); }
  static inline uint16_t HostToLittleEndian(uint16_t val) { return htole16(val); }
  static inline uint8_t HostToLittleEndian(uint8_t val) { return val; }
  static inline uint32_t LittleEndianToHost(uint32_t val) { return letoh32(val); }
  static inline uint16_t LittleEndianToHost(uint16_t val) { return letoh16(val); }
  static inline uint8_t LittleEndianToHost(uint8_t val) { return val; }
};

// An instance of I2cRegisterAddr represents a typed register address: It
// holds the address of the register in the i2c device and
// the type of its contents, RegType.  RegType represents the register's
// bitfields.  RegType should be a subclass of I2cRegisterBase.
//
// All usage rules of RegisterAddr applies here with the following exceptions
// - FromValue() is the only valid method for creating RegType.
// - reg_addr is stored in uint32_t and will be casted to the exact length in I2cRegisterBase.
template <class RegType>
class I2cRegisterAddr : public RegisterAddr<RegType> {
 public:
  static_assert(
      std::is_base_of<I2cRegisterBase<RegType, typename RegType::ValueType, RegType::AddrSize,
                                      typename RegType::I2cByteOrder>,
                      RegType>::value ||
          std::is_base_of<I2cRegisterBase<RegType, typename RegType::ValueType, RegType::AddrSize,
                                          typename RegType::I2cByteOrder, EnablePrinter>,
                          RegType>::value,
      "Parameter of I2cRegisterAddr<> should derive from I2cRegisterBase");

  // Delete base class ReadFrom() as read from i2c can fail and
  // cannot be used for constructing RegType.
  template <typename T>
  RegType ReadFrom(T* reg_io) = delete;

  I2cRegisterAddr(uint32_t reg_addr) : RegisterAddr<RegType>(reg_addr) {}
};
}  // namespace hwreg

#endif  // HWREG_I2C_H_
