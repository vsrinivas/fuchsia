// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTING_FAKE_MMIO_REG_INCLUDE_FAKE_MMIO_REG_FAKE_MMIO_REG_H_
#define SRC_DEVICES_TESTING_FAKE_MMIO_REG_INCLUDE_FAKE_MMIO_REG_FAKE_MMIO_REG_H_

#include <lib/fit/function.h>
#include <lib/mmio-ptr/fake.h>
#include <lib/mmio/mmio.h>

#include <fbl/vector.h>

namespace ddk_fake {

// Fakes a single MMIO register. This class is intended to be used with a ddk::MmioBuffer;
// operations on an instance of that class will be directed to the fake if the fake-mmio-reg library
// is a dependency of the test. The base address used by the MmioBuffer should be an array of
// FakeMmioReg objects. See the following example test:
//
// ddk_fake::FakeMmioReg register_array[number_of_registers];
// ddk_fake::FakeMmioRegRegion fake_registers(register_array, register_size, number_of_registers);
// ddk::MmioBuffer mmio_buffer(fake_registers.GetMmioBuffer());
// fake_registers[0].SetReadCallback(read_fn);
// fake_registers[0].SetWriteCallback(write_fn);
// SomeDriver dut(mmio_buffer);
// (put your test here)

class FakeMmioReg {
 public:
  // Reads from the faked register. Returns the value set by the next expectation, or the default
  // value. The default is initially zero and can be set by calling ReadReturns() or Write(). This
  // method is expected to be called (indirectly) by the code under test.
  FakeMmioReg() {
    read_ = []() { return 0; };
    write_ = [](uint64_t value) {

    };
  }
  void SetReadCallback(fit::function<uint64_t()> read) { read_ = std::move(read); }

  void SetWriteCallback(fit::function<void(uint64_t)> write) { write_ = std::move(write); }

  uint64_t Read() { return read_(); }

  // Writes to the faked register. This method is expected to be called (indirectly) by the code
  // under test.
  void Write(uint64_t value) { write_(value); }

 private:
  fit::function<void(uint64_t value)> write_;
  fit::function<uint64_t()> read_;
};

// Represents an array of FakeMmioReg objects.
class FakeMmioRegRegion {
 public:
  // Constructs a FakeMmioRegRegion backed by the given array. reg_size is the size of each
  // register in bytes, reg_count is the total size of the region in bytes. Ownership of fake_regs
  // is not transferred.
  FakeMmioRegRegion(FakeMmioReg* fake_regs, size_t reg_size, size_t reg_count)
      : fake_regs_(fake_regs), reg_size_(reg_size), reg_count_(reg_count) {
    ZX_ASSERT(reg_size_ > 0);
  }

  // Accesses the FakeMmioReg at the given offset. Note that this is the _offset_, not the
  // _index_.
  FakeMmioReg& operator[](size_t offset) const {
    ZX_ASSERT(offset / reg_size_ < reg_count_);
    return fake_regs_[offset / reg_size_];
  }

  // Returns an mmio_buffer_t that can be used for constructing a ddk::MmioBuffer object.
  ddk::MmioBuffer GetMmioBuffer() {
    return ddk::MmioBuffer(
        mmio_buffer_t{
            .vaddr = FakeMmioPtr(this),
            .offset = 0,
            .size = reg_size_ * reg_count_,
            .vmo = ZX_HANDLE_INVALID,
        },
        &kFakeMmioOps, this);
  }

 private:
  static uint8_t Read8(const void* ctx, const mmio_buffer_t& mmio, zx_off_t offs) {
    const auto& reg_region = *reinterpret_cast<const FakeMmioRegRegion*>(ctx);
    return static_cast<uint8_t>(reg_region[offs + mmio.offset].Read());
  }

  static uint16_t Read16(const void* ctx, const mmio_buffer_t& mmio, zx_off_t offs) {
    const auto& reg_region = *reinterpret_cast<const FakeMmioRegRegion*>(ctx);
    return static_cast<uint16_t>(reg_region[offs + mmio.offset].Read());
  }

  static uint32_t Read32(const void* ctx, const mmio_buffer_t& mmio, zx_off_t offs) {
    const auto& reg_region = *reinterpret_cast<const FakeMmioRegRegion*>(ctx);
    return static_cast<uint32_t>(reg_region[offs + mmio.offset].Read());
  }

  static uint64_t Read64(const void* ctx, const mmio_buffer_t& mmio, zx_off_t offs) {
    const auto& reg_region = *reinterpret_cast<const FakeMmioRegRegion*>(ctx);
    return reg_region[offs + mmio.offset].Read();
  }

  static void Write8(const void* ctx, const mmio_buffer_t& mmio, uint8_t val, zx_off_t offs) {
    Write64(ctx, mmio, val, offs);
  }

  static void Write16(const void* ctx, const mmio_buffer_t& mmio, uint16_t val, zx_off_t offs) {
    Write64(ctx, mmio, val, offs);
  }

  static void Write32(const void* ctx, const mmio_buffer_t& mmio, uint32_t val, zx_off_t offs) {
    Write64(ctx, mmio, val, offs);
  }

  static void Write64(const void* ctx, const mmio_buffer_t& mmio, uint64_t val, zx_off_t offs) {
    const auto& reg_region = *reinterpret_cast<const FakeMmioRegRegion*>(ctx);
    reg_region[offs + mmio.offset].Write(val);
  }

  static constexpr ddk::MmioBufferOps kFakeMmioOps = {
      .Read8 = Read8,
      .Read16 = Read16,
      .Read32 = Read32,
      .Read64 = Read64,
      .Write8 = Write8,
      .Write16 = Write16,
      .Write32 = Write32,
      .Write64 = Write64,
  };

  FakeMmioReg* fake_regs_;
  const size_t reg_size_;
  const size_t reg_count_;
};

}  // namespace ddk_fake

#endif  // SRC_DEVICES_TESTING_FAKE_MMIO_REG_INCLUDE_FAKE_MMIO_REG_FAKE_MMIO_REG_H_
