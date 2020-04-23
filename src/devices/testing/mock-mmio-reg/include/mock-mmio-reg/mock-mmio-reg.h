// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTING_MOCK_MMIO_REG_INCLUDE_MOCK_MMIO_REG_MOCK_MMIO_REG_H_
#define SRC_DEVICES_TESTING_MOCK_MMIO_REG_INCLUDE_MOCK_MMIO_REG_MOCK_MMIO_REG_H_

#include <lib/mmio/mmio.h>

#include <memory>

#include <fbl/vector.h>
#include <zxtest/zxtest.h>

namespace ddk_mock {

// Mocks a single MMIO register. This class is intended to be used with a ddk::MmioBuffer;
// operations on an instance of that class will be directed to the mock if the mock-mmio-reg library
// is a dependency of the test. The base address used by the MmioBuffer should be an array of
// MockMmioReg objects. See the following example test:
//
// ddk_mock::MockMmioReg register_array[number_of_registers];
// ddk_mock::MockMmioRegRegion mock_registers(register_array, register_size, number_of_registers);
// ddk::MmioBuffer mmio_buffer(mock_registers.GetMmioBuffer());
//
// SomeDriver dut(mmio_buffer);
// mock_registers[0]
//     .ExpectRead()
//     .ExpectWrite(0xdeadbeef)
//     .ExpectRead(0xcafecafe)
//     .ExpectWrite()
//     .ExpectRead();
// mock_registers[5]
//     .ExpectWrite(0)
//     .ExpectWrite(1024)
//     .ReadReturns(0);
//
// EXPECT_OK(dut.SomeMethod());
// mock_registers.VerifyAll();

class MockMmioReg {
 public:
  // Reads from the mocked register. Returns the value set by the next expectation, or the default
  // value. The default is initially zero and can be set by calling ReadReturns() or Write(). This
  // method is expected to be called (indirectly) by the code under test.
  uint64_t Read() {
    if (read_expectations_index_ >= read_expectations_.size()) {
      return last_value_;
    }

    MmioExpectation& exp = read_expectations_[read_expectations_index_++];
    if (exp.match == MmioExpectation::Match::kAny) {
      return last_value_;
    }

    return last_value_ = exp.value;
  }

  // Writes to the mocked register. This method is expected to be called (indirectly) by the code
  // under test.
  void Write(uint64_t value) {
    last_value_ = value;

    if (write_expectations_index_ >= write_expectations_.size()) {
      return;
    }

    MmioExpectation& exp = write_expectations_[write_expectations_index_++];
    if (exp.match != MmioExpectation::Match::kAny) {
      EXPECT_EQ(exp.value, value);
    }
  }

  // Matches a register read and returns the specified value.
  MockMmioReg& ExpectRead(uint64_t value) {
    read_expectations_.push_back(
        MmioExpectation{.match = MmioExpectation::Match::kEqual, .value = value});

    return *this;
  }

  // Matches a register read and returns the default value.
  MockMmioReg& ExpectRead() {
    read_expectations_.push_back(
        MmioExpectation{.match = MmioExpectation::Match::kAny, .value = 0});

    return *this;
  }

  // Sets the default register read value.
  MockMmioReg& ReadReturns(uint64_t value) {
    last_value_ = value;
    return *this;
  }

  // Matches a register write with the specified value.
  MockMmioReg& ExpectWrite(uint64_t value) {
    write_expectations_.push_back(
        MmioExpectation{.match = MmioExpectation::Match::kEqual, .value = value});

    return *this;
  }

  // Matches any register write.
  MockMmioReg& ExpectWrite() {
    write_expectations_.push_back(
        MmioExpectation{.match = MmioExpectation::Match::kAny, .value = 0});

    return *this;
  }

  // Removes and ignores all expectations and resets the default read value.
  void Clear() {
    last_value_ = 0;

    read_expectations_index_ = 0;
    while (read_expectations_.size() > 0) {
      read_expectations_.pop_back();
    }

    write_expectations_index_ = 0;
    while (write_expectations_.size() > 0) {
      write_expectations_.pop_back();
    }
  }

  // Removes all expectations and resets the default value. The presence of any outstanding
  // expectations causes a test failure.
  void VerifyAndClear() {
    EXPECT_GE(read_expectations_index_, read_expectations_.size());
    EXPECT_GE(write_expectations_index_, write_expectations_.size());
    Clear();
  }

 private:
  struct MmioExpectation {
    enum class Match { kEqual, kAny } match;
    uint64_t value;
  };

  uint64_t last_value_ = 0;

  size_t read_expectations_index_ = 0;
  fbl::Vector<MmioExpectation> read_expectations_;

  size_t write_expectations_index_ = 0;
  fbl::Vector<MmioExpectation> write_expectations_;
};

// Represents an array of MockMmioReg objects.
class MockMmioRegRegion {
 public:
  // Constructs a MockMmioRegRegion backed by the given array. reg_size is the size of each
  // register in bytes, reg_count is the total size of the region in bytes. Ownership of mock_regs
  // is not transferred.
  MockMmioRegRegion(MockMmioReg* mock_regs, size_t reg_size, size_t reg_count)
      : mock_regs_(mock_regs), reg_size_(reg_size), reg_count_(reg_count) {
    ASSERT_GT(reg_size_, 0);
  }

  // Accesses the MockMmioReg at the given offset. Note that this is the _offset_, not the
  // _index_.
  MockMmioReg& operator[](size_t offset) const {
    CheckOffset(offset);
    return mock_regs_[offset / reg_size_];
  }

  // Calls VerifyAndClear() on all MockMmioReg objects.
  void VerifyAll() {
    for (size_t i = 0; i < reg_count_; i++) {
      mock_regs_[i].VerifyAndClear();
    }
  }

  ddk::MmioBuffer GetMmioBuffer() {
    return ddk::MmioBuffer(
        mmio_buffer_t{
            .vaddr = this,
            .offset = 0,
            .size = reg_size_ * reg_count_,
            .vmo = ZX_HANDLE_INVALID,
        },
        &kMockMmioOps, this);
  }

 private:
  static uint8_t Read8(const void* ctx, const mmio_buffer_t& mmio, zx_off_t offs) {
    const auto& reg_region = *reinterpret_cast<const MockMmioRegRegion*>(ctx);
    return static_cast<uint8_t>(reg_region[offs + mmio.offset].Read());
  }

  static uint16_t Read16(const void* ctx, const mmio_buffer_t& mmio, zx_off_t offs) {
    const auto& reg_region = *reinterpret_cast<const MockMmioRegRegion*>(ctx);
    return static_cast<uint16_t>(reg_region[offs + mmio.offset].Read());
  }

  static uint32_t Read32(const void* ctx, const mmio_buffer_t& mmio, zx_off_t offs) {
    const auto& reg_region = *reinterpret_cast<const MockMmioRegRegion*>(ctx);
    return static_cast<uint32_t>(reg_region[offs + mmio.offset].Read());
  }

  static uint64_t Read64(const void* ctx, const mmio_buffer_t& mmio, zx_off_t offs) {
    const auto& reg_region = *reinterpret_cast<const MockMmioRegRegion*>(ctx);
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
    const auto& reg_region = *reinterpret_cast<const MockMmioRegRegion*>(ctx);
    reg_region[offs + mmio.offset].Write(val);
  }

  static constexpr ddk::MmioBufferOps kMockMmioOps = {
      .Read8 = Read8,
      .Read16 = Read16,
      .Read32 = Read32,
      .Read64 = Read64,
      .Write8 = Write8,
      .Write16 = Write16,
      .Write32 = Write32,
      .Write64 = Write64,
  };

  void CheckOffset(zx_off_t offs) const { ASSERT_LT(offs / reg_size_, reg_count_); }

  MockMmioReg* const mock_regs_;
  const size_t reg_size_;
  const size_t reg_count_;
};

}  // namespace ddk_mock

#endif  // SRC_DEVICES_TESTING_MOCK_MMIO_REG_INCLUDE_MOCK_MMIO_REG_MOCK_MMIO_REG_H_
