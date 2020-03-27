// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HWREG_INDIRECT_H_
#define HWREG_INDIRECT_H_

#include <limits.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/threads.h>

#include <mutex>
#include <type_traits>

#include <hwreg/mmio.h>

namespace hwreg {

// IndirectIo is a replacement for RegisterMmio that sequences access to a common PCI device construct
// where two registers, "index" and "data", are used to read and write a much larger store,
// logically private_store[index] = data.
//
// Example usage from VGA:
//
// typedef IndirectIo<0x03D4, 0x03D5> CrtcIo;
// class CrtcReg : public hwreg::RegisterBase<CrtcReg, uint8_t> {
//  public:
//   static auto Get(size_t offset) : { return hwreg::RegisterAddr<CrtcReg>(offset); }
// };
// ...
// void SetMode3(volatile void* mmio) {
//   auto io = CrtcIo(mmio);
//   CrtcReg::Get(0x00).ReadFrom(&io).set_reg_value(0x5F).WriteTo(&io);
//   CrtcReg::Get(0x01).ReadFrom(&io).set_reg_value(0x4F).WriteTo(&io);
//   CrtcReg::Get(0x02).ReadFrom(&io).set_reg_value(0x50).WriteTo(&io);
//   CrtcReg::Get(0x03).ReadFrom(&io).set_reg_value(0x82).WriteTo(&io);
//   CrtcReg::Get(0x04).ReadFrom(&io).set_reg_value(0x55).WriteTo(&io);
//   CrtcReg::Get(0x05).ReadFrom(&io).set_reg_value(0x81).WriteTo(&io);
// }
template <uint32_t index_offset, uint32_t data_offset, typename IndexType = uint8_t, typename RegisterIo = RegisterMmio>
class IndirectIo {
 public:
  explicit IndirectIo(RegisterIo io) : io_(std::move(io)) {}

  ~IndirectIo() = default;

  template <class IntType>
  void Write(IntType value, uint32_t offset) {
    std::lock_guard<std::mutex> l(lock_);
    io_.template Write<IndexType>(static_cast<IndexType>(offset), index_offset);
    io_.template Write(value, data_offset);
  }

  template <class IntType>
  IntType Read(uint32_t offset) {
    std::lock_guard<std::mutex> l(lock_);
    io_.template Write<IndexType>(static_cast<IndexType>(offset), index_offset);
    return io_.template Read<IntType>(data_offset);
  }

 private:
  std::mutex lock_;
  RegisterIo io_ __TA_GUARDED(lock_);
};

}  // namespace hwreg

#endif  // HWREG_INDIRECT_H_
