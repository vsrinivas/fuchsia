// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test for the debug interfaces specific to processes.

#include <stdint.h>

#include <lib/zx/process.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>
#include <zxtest/zxtest.h>

constexpr size_t kVmoSize = 4096 * 3u;
constexpr size_t kVmarSize = kVmoSize * 2u;

namespace {
// xorshift PRNG, use as first value any number except 0. This simple looking function
// generates a random sequence with period 2^31 when fed its previous value.
uint32_t xorshift32(uint32_t prev) {
  prev ^= prev << 13;
  prev ^= prev >> 17;
  prev ^= prev << 5;
  return prev;
}

std::unique_ptr<uint32_t[]> MakeXorShiftBuf(size_t len) {
  const auto count = len / sizeof(uint32_t);
  auto buf = std::make_unique<uint32_t[]>(count);

  auto xorv = xorshift32(1u);
  for (size_t ix = 0; ix != count; ++ix) {
    buf[ix] = xorv;
    xorv = xorshift32(xorv);
  }
  return buf;
}

bool VerifyXorShiftBuf(const uint32_t* data, size_t len) {
  const auto count = len / sizeof(uint32_t);

  auto xorv = xorshift32(1u);
  for (size_t ix = 0; ix != count; ++ix) {
    if (data[ix] != xorv)
      return false;
    xorv = xorshift32(xorv);
  }
  return true;
}

bool VerifyXorShiftVmo(const zx::vmo& vmo, size_t len) {
  const auto count = len / sizeof(uint32_t);
  auto buf = std::make_unique<uint32_t[]>(count);

  if (vmo.read(buf.get(), 0, len) != ZX_OK) {
    return false;
  }
  return VerifyXorShiftBuf(buf.get(), len);
}

// This fixture creates a VMAR and a VMO mapped into it for the current process.
class ProcessDebugFixture : public zxtest::Test {
 public:
  const zx::vmo& vmo() { return vmo_; }
  const zx::vmar& vmar() { return vmar_; }
  zx_vaddr_t data_start() { return map_addr_; }
  zx_vaddr_t vmar_start() { return vmar_addr_; }

 private:
  static constexpr zx_vm_option_t kVmarOpts = ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE;
  static constexpr zx_vm_option_t kMapOpts = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;

  void SetUp() override {
    ASSERT_OK(zx::vmo::create(kVmoSize, 0u, &vmo_));
    ASSERT_OK(zx::vmar::root_self()->allocate2(kVmarOpts, 0u, kVmarSize, &vmar_, &vmar_addr_));
    ASSERT_OK(vmar_.map(kMapOpts, 0u, vmo_, 0u, kVmoSize, &map_addr_));
  }

  zx::vmo vmo_;
  zx::vmar vmar_;
  zx_vaddr_t map_addr_ = 0u;
  zx_vaddr_t vmar_addr_ = 0u;
};

using ProcessDebugTest = ProcessDebugFixture;

TEST(ProcessDebugUtilsTest, XorShiftIsOk) {
  ASSERT_EQ(270369u, xorshift32(1u));
  ASSERT_EQ(67634689u, xorshift32(270369u));
}

TEST_F(ProcessDebugTest, ReadMemoryAtOffsetIsOk) {
  // Write pattern via VMO and read it via zx_process_read_memory().
  auto xr = MakeXorShiftBuf(kVmoSize);
  ASSERT_OK(vmo().write(xr.get(), 0u, kVmoSize));

  auto buf = std::make_unique<uint32_t[]>(kVmoSize);
  size_t actual = 0u;
  ASSERT_OK(zx::process::self()->read_memory(data_start(), buf.get(), kVmoSize, &actual));
  ASSERT_EQ(actual, kVmoSize);
  ASSERT_TRUE(VerifyXorShiftBuf(buf.get(), kVmoSize));
}

TEST_F(ProcessDebugTest, WriteMemoryAtOffsetIsOk) {
  // Write pattern via zx_process_write_memory() and read via vmo.
  auto xr = MakeXorShiftBuf(kVmoSize);
  size_t actual = 0u;
  ASSERT_OK(zx::process::self()->write_memory(data_start(), xr.get(), kVmoSize, &actual));
  ASSERT_TRUE(VerifyXorShiftVmo(vmo(), kVmoSize));
}

TEST_F(ProcessDebugTest, ReadMemoryAtInvalidOffsetReturnsErrorNoMemory) {
  char buf[64];
  size_t actual = 0u;
  ASSERT_EQ(zx::process::self()->read_memory(0u, buf, 64, &actual), ZX_ERR_NO_MEMORY);
  // Either the first page or the last page of the mapping is invalid, use that address.
  auto read_start =
      (data_start() > vmar_start()) ? vmar_start() : vmar_start() + kVmarSize - ZX_PAGE_SIZE;
  ASSERT_EQ(zx::process::self()->read_memory(read_start, buf, 64, &actual), ZX_ERR_NO_MEMORY);
}

TEST_F(ProcessDebugTest, WriteAtInvalidOffsetReturnsErrorNoMemory) {
  const char buf[64] = {0};
  size_t actual = 0u;
  ASSERT_EQ(zx::process::self()->write_memory(0u, buf, 64, &actual), ZX_ERR_NO_MEMORY);
  // Either the first page or the last page of the mapping is invalid, use that address.
  auto write_start =
      (data_start() > vmar_start()) ? vmar_start() : vmar_start() + kVmarSize - ZX_PAGE_SIZE;
  ASSERT_EQ(zx::process::self()->write_memory(write_start, buf, 64, &actual), ZX_ERR_NO_MEMORY);
}

TEST(ProcessDebugVDSO, WriteToVdsoAddressReturnsAccessDenied) {
  const uintptr_t code_addr[] = {reinterpret_cast<uintptr_t>(zx_channel_write),
                                 reinterpret_cast<uintptr_t>(zx_handle_close),
                                 reinterpret_cast<uintptr_t>(zx_ticks_per_second),
                                 reinterpret_cast<uintptr_t>(zx_deadline_after)};
  // If the kernel code gets this wrong, the expected result is a hard kernel panic.
  size_t actual = 0;
  const char buf[64] = {0x1c};
  for (auto addr : code_addr) {
    ASSERT_EQ(zx::process::self()->write_memory(addr, buf, 64, &actual), ZX_ERR_ACCESS_DENIED);
  }
}

}  // namespace
