// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_MOCK_MMIO_RANGE_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_MOCK_MMIO_RANGE_H_

#include <lib/mmio-ptr/fake.h>
#include <lib/mmio/mmio.h>
#include <lib/stdcompat/span.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <cstdint>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>
#include <gtest/gtest.h>

namespace i915_tgl {

// TODO(fxbug.dev/105644): Move this file to a directory where it can be reused
//                         by all drivers, or deprecate in favor of a library in
//                         the SDK.

// An MMIO range that responds to a list of pre-determined memory accesses.
//
// MockMmioRange enforces a global ordering on all accesses to the mocked MMIO
// range. This is stricter than MockMmioRegRegion, which accepts any
// interleaving of the access lists specified at the register level. So,
// MockMmioRange results in more brittle mocks, and should only be used when
// there is a single acceptable access ordering.
//
// TODO(fxbug.dev/105647): Instances are thread-safe. This hides unsafe
// concurrent MMIO accesses from TSAN. We should figure out a better thread
// safety story.
//
// Example usage:
//   constexpr static size_t kMmioRangeSize = 0x4000;
//   MockMmioRange range_{kMmioRangeSize, MockMmioRange::Size::k32};
//   fdf::MmioBuffer buffer_{range_.GetMmioBuffer()};
//
//   // Expect a 32-bit read at 0x1000, the read will return 0x12345678.
//   range_.Expect({.address = 0x1000, .value = 0x12345678});
//   // Expect a 32-bit write of 0x87654321 at 0x1002
//   range_.Expect({.address = 0x1002, .value = 0x87654321, .write = true});
//
//   // Test polling for a ready flag at 0x1004.
//   range_.Expect(MockMmioRange::AccessList({
//       {.address = 0x1004, .value = 0x0},
//       {.address = 0x1004, .value = 0x0},
//       {.address = 0x1004, .value = 0x0},
//       {.address = 0x1004, .value = 0x1},
//   }));
//
//   // This could go in TearDopwn().
//   range_.CheckAllAccessesReplayed();
//
// The following practices are not required, but are consistent with the
// recommendation of keeping testing logic simple:
//
// * Expect() calls should be at the beginning of the test case, before
//   executing the code that accesses the MMIO range.
// * A test's expectations should be grouped in a single Expect() call. In rare
//   cases, multiple cases and conditional logic may improve readability.
// * Expect() should not be called concurrently from multiple threads.
class MockMmioRange {
 public:
  // The supported MMIO access sizes.
  enum class Size {
    kUseDefault = 0,
    k8 = 1,   // fdf::MmioBuffer::Read8(), fdf::MmioBuffer::Write8().
    k16 = 2,  // fdf::MmioBuffer::Read16(), fdf::MmioBuffer::Write16().
    k32 = 4,  // fdf::MmioBuffer::Read32(), fdf::MmioBuffer::Write32().
    k64 = 8,  // fdf::MmioBuffer::Read64(), fdf::MmioBuffer::Write64().
  };

  // Information about an expected MMIO access. Passed into Expect().
  struct Access {
    zx_off_t address;
    uint64_t value;  // Expected by writes, returned by reads.
    bool write = false;
    Size size = Size::kUseDefault;  // Use default value size.
  };

  // Alias for conveniently calling Expect() with multiple accesses.
  using AccessList = cpp20::span<const Access>;

  // `default_access_size` is used for Access instances whose `size` is
  // `kUseDefault`.
  explicit MockMmioRange(size_t range_size, Size default_access_size = Size::k32)
      : range_size_(range_size), default_access_size_(default_access_size) {}
  ~MockMmioRange() = default;

  // Appends an entry to the list of expected memory accesses.
  //
  // To keep the testing logic simple, all Expect() calls should be performed
  // before executing the code that uses the MMIO range.
  void Expect(const Access& access) { Expect(cpp20::span<const Access>({access})); }

  // Appends the given entries to the list of expected memory accesses.
  //
  // To keep the testing logic simple, all Expect() calls should be performed
  // before executing the code that uses the MMIO range.
  void Expect(cpp20::span<const Access> accesses) {
    fbl::AutoLock<fbl::Mutex> lock(&mutex_);
    for (const auto& access : accesses) {
      access_list_.push_back({
          .address = access.address,
          .value = access.value,
          .write = access.write,
          .size = access.size == Size::kUseDefault ? default_access_size_ : access.size,
      });
    }
  }

  // Asserts that the entire memory access list has been replayed.
  void CheckAllAccessesReplayed() {
    fbl::AutoLock<fbl::Mutex> lock(&mutex_);
    EXPECT_EQ(access_list_.size(), access_index_);
  }

  fdf::MmioBuffer GetMmioBuffer() {
    static constexpr fdf::internal::MmioBufferOps kMockMmioOps = {
        .Read8 = MockMmioRange::Read8,
        .Read16 = MockMmioRange::Read16,
        .Read32 = MockMmioRange::Read32,
        .Read64 = MockMmioRange::Read64,
        .Write8 = MockMmioRange::Write8,
        .Write16 = MockMmioRange::Write16,
        .Write32 = MockMmioRange::Write32,
        .Write64 = MockMmioRange::Write64,
    };
    return fdf::MmioBuffer(
        mmio_buffer_t{
            .vaddr = FakeMmioPtr(this),
            .offset = 0,
            .size = range_size_,
            .vmo = ZX_HANDLE_INVALID,
        },
        &kMockMmioOps, this);
  }

 private:
  // MmioBufferOps implementation.
  static uint8_t Read8(const void* ctx, const mmio_buffer_t&, zx_off_t offset) {
    return static_cast<uint8_t>(static_cast<const MockMmioRange*>(ctx)->Read(offset, Size::k8));
  }
  static uint16_t Read16(const void* ctx, const mmio_buffer_t&, zx_off_t offset) {
    return static_cast<uint16_t>(static_cast<const MockMmioRange*>(ctx)->Read(offset, Size::k16));
  }
  static uint32_t Read32(const void* ctx, const mmio_buffer_t&, zx_off_t offset) {
    return static_cast<uint32_t>(static_cast<const MockMmioRange*>(ctx)->Read(offset, Size::k32));
  }
  static uint64_t Read64(const void* ctx, const mmio_buffer_t&, zx_off_t offset) {
    return static_cast<const MockMmioRange*>(ctx)->Read(offset, Size::k64);
  }
  static void Write8(const void* ctx, const mmio_buffer_t&, uint8_t value, zx_off_t offset) {
    static_cast<const MockMmioRange*>(ctx)->Write(offset, value, Size::k8);
  }
  static void Write16(const void* ctx, const mmio_buffer_t&, uint16_t value, zx_off_t offset) {
    static_cast<const MockMmioRange*>(ctx)->Write(offset, value, Size::k16);
  }
  static void Write32(const void* ctx, const mmio_buffer_t&, uint32_t value, zx_off_t offset) {
    static_cast<const MockMmioRange*>(ctx)->Write(offset, value, Size::k32);
  }
  static void Write64(const void* ctx, const mmio_buffer_t&, uint64_t value, zx_off_t offset) {
    static_cast<const MockMmioRange*>(ctx)->Write(offset, value, Size::k64);
  }

  uint64_t Read(zx_off_t address, Size size) const {
    fbl::AutoLock<fbl::Mutex> lock(&mutex_);
    if (access_index_ >= access_list_.size()) {
      // Google Test's ASSERT_*() macros only work in void functions.
      EXPECT_FALSE("MMIO read after access list consumed");
      return 0;
    }
    Access& expected_access = access_list_[access_index_];
    ++access_index_;

    EXPECT_EQ(expected_access.write, false);
    EXPECT_EQ(expected_access.address, address);
    EXPECT_EQ(expected_access.size, size);
    return expected_access.value;
  }

  void Write(zx_off_t address, uint64_t value, Size size) const {
    fbl::AutoLock<fbl::Mutex> lock(&mutex_);
    if (access_index_ >= access_list_.size()) {
      EXPECT_FALSE("MMIO read after access list consumed");
      return;
    }
    Access& expected_access = access_list_[access_index_];
    ++access_index_;

    EXPECT_EQ(expected_access.write, true);
    EXPECT_EQ(expected_access.address, address);
    EXPECT_EQ(expected_access.size, size);
    EXPECT_EQ(expected_access.value, value);
  }

  mutable fbl::Mutex mutex_;
  mutable fbl::Vector<Access> access_list_ __TA_GUARDED(mutex_);
  mutable size_t access_index_ __TA_GUARDED(mutex_) = 0;
  const size_t range_size_;
  const Size default_access_size_;
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_MOCK_MMIO_RANGE_H_
