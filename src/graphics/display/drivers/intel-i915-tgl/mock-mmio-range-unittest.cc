// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/mock-mmio-range.h"

#include <lib/mmio/mmio.h>

#include <cstdint>

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

namespace i915_tgl {

namespace {

// TODO(fxbug.dev/105644): Move this file to a directory where it can be reused
//                         by all drivers, or deprecate in favor of a library in
//                         the SDK.

class MockMmioRangeTest : public ::testing::Test {
 public:
  void TearDown() override { mmio_range_.CheckAllAccessesReplayed(); }

 protected:
  MockMmioRange mmio_range_{0x4000, MockMmioRange::Size::k32};
  fdf::MmioBuffer mmio_buffer_{mmio_range_.GetMmioBuffer()};
};

TEST_F(MockMmioRangeTest, NoOperations) {
  MockMmioRange mmio_range(0x1000, MockMmioRange::Size::k32);
  mmio_range_.CheckAllAccessesReplayed();
  SUCCEED();
}

TEST_F(MockMmioRangeTest, ConstructorRangeSize) {
  MockMmioRange mmio_range1(0x1000, MockMmioRange::Size::k32);
  MockMmioRange mmio_range2(0x4000, MockMmioRange::Size::k32);

  fdf::MmioBuffer mmio_range1_buffer = mmio_range1.GetMmioBuffer();
  fdf::MmioBuffer mmio_range2_buffer = mmio_range2.GetMmioBuffer();

  EXPECT_EQ(0x1000u, mmio_range1_buffer.get_size());
  EXPECT_EQ(0x4000u, mmio_range2_buffer.get_size());
}

TEST_F(MockMmioRangeTest, ConstructorDefaultOperationSize) {
  MockMmioRange mmio_range1(0x1000, MockMmioRange::Size::k32);
  MockMmioRange mmio_range2(0x1000, MockMmioRange::Size::k16);

  fdf::MmioBuffer mmio_range1_buffer = mmio_range1.GetMmioBuffer();
  fdf::MmioBuffer mmio_range2_buffer = mmio_range2.GetMmioBuffer();

  mmio_range1.Expect({.address = 0x100, .value = 0x01});
  mmio_range2.Expect({.address = 0x100, .value = 0x01});

  EXPECT_EQ(0x01u, mmio_range1_buffer.Read32(0x100));
  EXPECT_EQ(0x01u, mmio_range2_buffer.Read16(0x100));
}

TEST_F(MockMmioRangeTest, ReadOnce) {
  mmio_range_.Expect({.address = 0x100, .value = 0x42434445});
  EXPECT_EQ(0x42434445u, mmio_buffer_.Read32(0x100));
}

TEST_F(MockMmioRangeTest, ReadOnceNonDefaultSize) {
  mmio_range_.Expect({.address = 0x100, .value = 0x42434445, .size = MockMmioRange::Size::k64});
  EXPECT_EQ(0x42434445u, mmio_buffer_.Read64(0x100));
}

TEST_F(MockMmioRangeTest, ReadOnceExplicitSize) {
  mmio_range_.Expect({.address = 0x100, .value = 0x42434445, .size = MockMmioRange::Size::k32});
  EXPECT_EQ(0x42434445u, mmio_buffer_.Read32(0x100));
}

TEST_F(MockMmioRangeTest, ReadRepeated) {
  mmio_range_.Expect({.address = 0x100, .value = 0x42});
  mmio_range_.Expect({.address = 0x100, .value = 0x43});
  mmio_range_.Expect({.address = 0x100, .value = 0x44});
  mmio_range_.Expect({.address = 0x100, .value = 0x45});

  EXPECT_EQ(0x42u, mmio_buffer_.Read32(0x100));
  EXPECT_EQ(0x43u, mmio_buffer_.Read32(0x100));
  EXPECT_EQ(0x44u, mmio_buffer_.Read32(0x100));
  EXPECT_EQ(0x45u, mmio_buffer_.Read32(0x100));
}

TEST_F(MockMmioRangeTest, ReadRepeatedFromAccessList) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = 0x100, .value = 0x42},
      {.address = 0x100, .value = 0x43},
      {.address = 0x100, .value = 0x44},
      {.address = 0x100, .value = 0x45},
  }));

  EXPECT_EQ(0x42u, mmio_buffer_.Read32(0x100));
  EXPECT_EQ(0x43u, mmio_buffer_.Read32(0x100));
  EXPECT_EQ(0x44u, mmio_buffer_.Read32(0x100));
  EXPECT_EQ(0x45u, mmio_buffer_.Read32(0x100));
}

TEST_F(MockMmioRangeTest, ReadVaryingAddressSize) {
  mmio_range_.Expect({.address = 0x100, .value = 0x42});
  mmio_range_.Expect({.address = 0x104, .value = 0x43, .size = MockMmioRange::Size::k16});
  mmio_range_.Expect({.address = 0x106, .value = 0x44, .size = MockMmioRange::Size::k8});
  mmio_range_.Expect({.address = 0x108, .value = 0x45, .size = MockMmioRange::Size::k64});

  EXPECT_EQ(0x42u, mmio_buffer_.Read32(0x100));
  EXPECT_EQ(0x43u, mmio_buffer_.Read16(0x104));
  EXPECT_EQ(0x44u, mmio_buffer_.Read8(0x106));
  EXPECT_EQ(0x45u, mmio_buffer_.Read64(0x108));
}

TEST_F(MockMmioRangeTest, ReadVaryingAddressSizeFromAccessLists) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = 0x100, .value = 0x42},
      {.address = 0x104, .value = 0x43, .size = MockMmioRange::Size::k16},
  }));
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = 0x106, .value = 0x44, .size = MockMmioRange::Size::k8},
      {.address = 0x108, .value = 0x45, .size = MockMmioRange::Size::k64},
  }));

  EXPECT_EQ(0x42u, mmio_buffer_.Read32(0x100));
  EXPECT_EQ(0x43u, mmio_buffer_.Read16(0x104));
  EXPECT_EQ(0x44u, mmio_buffer_.Read8(0x106));
  EXPECT_EQ(0x45u, mmio_buffer_.Read64(0x108));
}

TEST_F(MockMmioRangeTest, ReadMismatchedAddressLogsActualAccess) {
  MockMmioRange mmio_range(0x1000, MockMmioRange::Size::k32);
  fdf::MmioBuffer mmio_buffer = mmio_range.GetMmioBuffer();

  mmio_range.Expect({.address = 0x100, .value = 0x42434445});  // Matched.
  mmio_range.Expect({.address = 0x100, .value = 0x42434445});  // Not matched.

  EXPECT_EQ(0x42434445u, mmio_buffer.Read32(0x100));
  EXPECT_NONFATAL_FAILURE(std::ignore = mmio_buffer.Read32(0x222), "(546, false, 32)");
}

TEST_F(MockMmioRangeTest, ReadMismatchedAddressLogsExpectedAccess) {
  MockMmioRange mmio_range(0x1000, MockMmioRange::Size::k32);
  fdf::MmioBuffer mmio_buffer = mmio_range.GetMmioBuffer();

  mmio_range.Expect({.address = 0x100, .value = 0x42434445});  // Matched.
  mmio_range.Expect({.address = 0x100, .value = 0x42434445});  // Not matched.

  EXPECT_EQ(0x42434445u, mmio_buffer.Read32(0x100));
  EXPECT_NONFATAL_FAILURE(std::ignore = mmio_buffer.Read32(0x222), "(256, false, 32)");
}

TEST_F(MockMmioRangeTest, ReadMismatchedSizeLogsExpectedSize) {
  MockMmioRange mmio_range(0x1000, MockMmioRange::Size::k32);
  fdf::MmioBuffer mmio_buffer = mmio_range.GetMmioBuffer();

  mmio_range.Expect({.address = 0x100, .value = 0x42434445});
  mmio_range.Expect({.address = 0x100, .value = 0x42434445});

  EXPECT_EQ(0x42434445u, mmio_buffer.Read32(0x100));
  EXPECT_NONFATAL_FAILURE(std::ignore = mmio_buffer.Read64(0x100), "(256, false, 32)");
}

TEST_F(MockMmioRangeTest, ReadPastAccessListLogsMessage) {
  MockMmioRange mmio_range(0x1000, MockMmioRange::Size::k32);
  fdf::MmioBuffer mmio_buffer = mmio_range.GetMmioBuffer();

  mmio_range.Expect({.address = 0x100, .value = 0x42434445});
  mmio_range.Expect({.address = 0x100, .value = 0x42434445});

  EXPECT_EQ(0x42434445u, mmio_buffer.Read32(0x100));
  EXPECT_EQ(0x42434445u, mmio_buffer.Read32(0x100));
  EXPECT_NONFATAL_FAILURE(std::ignore = mmio_buffer.Read32(0x100),
                          "MMIO read after access list consumed");
}

TEST_F(MockMmioRangeTest, ReadPastAccessListLogsAccess) {
  MockMmioRange mmio_range(0x1000, MockMmioRange::Size::k32);
  fdf::MmioBuffer mmio_buffer = mmio_range.GetMmioBuffer();

  mmio_range.Expect({.address = 0x100, .value = 0x42434445});
  mmio_range.Expect({.address = 0x100, .value = 0x42434445});

  EXPECT_EQ(0x42434445u, mmio_buffer.Read32(0x100));
  EXPECT_EQ(0x42434445u, mmio_buffer.Read32(0x100));
  EXPECT_NONFATAL_FAILURE(std::ignore = mmio_buffer.Read32(0x100), "(256, 32, ");
}

TEST_F(MockMmioRangeTest, ReadExpectedWriteLogsExpectedAccess) {
  MockMmioRange mmio_range(0x1000, MockMmioRange::Size::k32);
  fdf::MmioBuffer mmio_buffer = mmio_range.GetMmioBuffer();

  mmio_range.Expect({.address = 0x100, .value = 0x42434445, .write = true});
  mmio_range.Expect({.address = 0x100, .value = 0x42434445, .write = true});

  mmio_buffer.Write32(0x42434445, 0x100);
  EXPECT_NONFATAL_FAILURE(std::ignore = mmio_buffer.Read32(0x100), "(256, true, 32)");
}

TEST_F(MockMmioRangeTest, WriteOnce) {
  mmio_range_.Expect({.address = 0x100, .value = 0x42434445, .write = true});
  mmio_buffer_.Write32(0x42434445, 0x100);
  mmio_range_.CheckAllAccessesReplayed();
  SUCCEED();
}

TEST_F(MockMmioRangeTest, WriteOnceNonDefaultSize) {
  mmio_range_.Expect({.address = 0x100, .value = 0x42434445, .size = MockMmioRange::Size::k64});
  EXPECT_EQ(0x42434445u, mmio_buffer_.Read64(0x100));
}

TEST_F(MockMmioRangeTest, WriteOnceExplicitSize) {
  mmio_range_.Expect({.address = 0x100, .value = 0x42434445, .size = MockMmioRange::Size::k32});
  EXPECT_EQ(0x42434445u, mmio_buffer_.Read32(0x100));
}

TEST_F(MockMmioRangeTest, WriteRepeated) {
  mmio_range_.Expect({.address = 0x100, .value = 0x42, .write = true});
  mmio_range_.Expect({.address = 0x100, .value = 0x43, .write = true});
  mmio_range_.Expect({.address = 0x100, .value = 0x44, .write = true});
  mmio_range_.Expect({.address = 0x100, .value = 0x45, .write = true});

  mmio_buffer_.Write32(0x42, 0x100);
  mmio_buffer_.Write32(0x43, 0x100);
  mmio_buffer_.Write32(0x44, 0x100);
  mmio_buffer_.Write32(0x45, 0x100);
}

TEST_F(MockMmioRangeTest, WriteRepeatedFromAccessList) {
  mmio_range_.Expect(MockMmioRange::AccessList({{.address = 0x100, .value = 0x42, .write = true},
                                                {.address = 0x100, .value = 0x43, .write = true},
                                                {.address = 0x100, .value = 0x44, .write = true},
                                                {.address = 0x100, .value = 0x45, .write = true}}));

  mmio_buffer_.Write32(0x42, 0x100);
  mmio_buffer_.Write32(0x43, 0x100);
  mmio_buffer_.Write32(0x44, 0x100);
  mmio_buffer_.Write32(0x45, 0x100);
}

TEST_F(MockMmioRangeTest, WriteVaryingAddressSize) {
  mmio_range_.Expect({.address = 0x100, .value = 0x42, .write = true});
  mmio_range_.Expect(
      {.address = 0x104, .value = 0x43, .write = true, .size = MockMmioRange::Size::k16});
  mmio_range_.Expect(
      {.address = 0x106, .value = 0x44, .write = true, .size = MockMmioRange::Size::k8});
  mmio_range_.Expect(
      {.address = 0x108, .value = 0x45, .write = true, .size = MockMmioRange::Size::k64});

  mmio_buffer_.Write32(0x42, 0x100);
  mmio_buffer_.Write16(0x43, 0x104);
  mmio_buffer_.Write8(0x44, 0x106);
  mmio_buffer_.Write64(0x45, 0x108);
}

TEST_F(MockMmioRangeTest, WriteVaryingAddressSizeFromAccessLists) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = 0x100, .value = 0x42, .write = true},
      {.address = 0x104, .value = 0x43, .write = true, .size = MockMmioRange::Size::k16},
  }));
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = 0x106, .value = 0x44, .write = true, .size = MockMmioRange::Size::k8},
      {.address = 0x108, .value = 0x45, .write = true, .size = MockMmioRange::Size::k64},
  }));

  mmio_buffer_.Write32(0x42, 0x100);
  mmio_buffer_.Write16(0x43, 0x104);
  mmio_buffer_.Write8(0x44, 0x106);
  mmio_buffer_.Write64(0x45, 0x108);
}

TEST_F(MockMmioRangeTest, WriteMismatchedAddressLogsActualAccess) {
  MockMmioRange mmio_range(0x1000, MockMmioRange::Size::k32);
  fdf::MmioBuffer mmio_buffer = mmio_range.GetMmioBuffer();

  mmio_range.Expect({.address = 0x100, .value = 0x42434445, .write = true});
  mmio_range.Expect({.address = 0x100, .value = 0x42434445, .write = true});

  mmio_buffer.Write32(0x42434445, 0x100);
  EXPECT_NONFATAL_FAILURE(mmio_buffer.Write32(0x42434445, 0x222), "(546, 1111704645, true, 32)");
}

TEST_F(MockMmioRangeTest, WriteMismatchedAddressLogsExpectedAccess) {
  MockMmioRange mmio_range(0x1000, MockMmioRange::Size::k32);
  fdf::MmioBuffer mmio_buffer = mmio_range.GetMmioBuffer();

  mmio_range.Expect({.address = 0x100, .value = 0x42434445, .write = true});
  mmio_range.Expect({.address = 0x100, .value = 0x42434445, .write = true});

  mmio_buffer.Write32(0x42434445, 0x100);
  EXPECT_NONFATAL_FAILURE(mmio_buffer.Write32(0x42434445, 0x222), "(256, 1111704645, true, 32)");
}

TEST_F(MockMmioRangeTest, WriteMismatchedSizeLogsExpectedAccess) {
  MockMmioRange mmio_range(0x1000, MockMmioRange::Size::k32);
  fdf::MmioBuffer mmio_buffer = mmio_range.GetMmioBuffer();

  mmio_range.Expect({.address = 0x100, .value = 0x42434445, .write = true});
  mmio_range.Expect({.address = 0x100, .value = 0x42434445, .write = true});

  mmio_buffer.Write32(0x42434445, 0x100);
  EXPECT_NONFATAL_FAILURE(mmio_buffer.Write64(0x42434445, 0x100), "(256, 1111704645, true, 32)");
}

TEST_F(MockMmioRangeTest, WritePastAccessListLogsMessage) {
  MockMmioRange mmio_range(0x1000, MockMmioRange::Size::k32);
  fdf::MmioBuffer mmio_buffer = mmio_range.GetMmioBuffer();

  mmio_range.Expect({.address = 0x100, .value = 0x42434445, .write = true});
  mmio_range.Expect({.address = 0x100, .value = 0x42434445, .write = true});

  mmio_buffer.Write32(0x42434445, 0x100);
  mmio_buffer.Write32(0x42434445, 0x100);
  EXPECT_NONFATAL_FAILURE(mmio_buffer.Write32(0x42434445, 0x100),
                          "MMIO write after access list consumed");
}

TEST_F(MockMmioRangeTest, WritePastAccessListLogsAccess) {
  MockMmioRange mmio_range(0x1000, MockMmioRange::Size::k32);
  fdf::MmioBuffer mmio_buffer = mmio_range.GetMmioBuffer();

  mmio_range.Expect({.address = 0x100, .value = 0x42434445, .write = true});
  mmio_range.Expect({.address = 0x100, .value = 0x42434445, .write = true});

  mmio_buffer.Write32(0x42434445, 0x100);
  mmio_buffer.Write32(0x42434445, 0x100);
  EXPECT_NONFATAL_FAILURE(mmio_buffer.Write32(0x42434445, 0x100), "(256, 1111704645, 32, ");
}

TEST_F(MockMmioRangeTest, WriteExpectedRead) {
  MockMmioRange mmio_range(0x1000, MockMmioRange::Size::k32);
  fdf::MmioBuffer mmio_buffer = mmio_range.GetMmioBuffer();

  mmio_range.Expect({.address = 0x100, .value = 0x42434445});
  mmio_range.Expect({.address = 0x100, .value = 0x42434445});

  EXPECT_EQ(0x42434445u, mmio_buffer.Read32(0x100));
  EXPECT_NONFATAL_FAILURE(mmio_buffer.Write32(0x42434445, 0x100), "(256, 1111704645, false, 32)");
}

TEST_F(MockMmioRangeTest, InterleavedReadAndWrite) {
  mmio_range_.Expect({.address = 0x100, .value = 0x42, .write = true});
  mmio_range_.Expect({.address = 0x104, .value = 0x43, .size = MockMmioRange::Size::k16});
  mmio_range_.Expect(
      {.address = 0x106, .value = 0x44, .write = true, .size = MockMmioRange::Size::k8});
  mmio_range_.Expect({.address = 0x108, .value = 0x45, .size = MockMmioRange::Size::k64});

  mmio_buffer_.Write32(0x42, 0x100);
  EXPECT_EQ(0x43u, mmio_buffer_.Read16(0x104));
  mmio_buffer_.Write8(0x44, 0x106);
  EXPECT_EQ(0x45u, mmio_buffer_.Read64(0x108));
}

TEST_F(MockMmioRangeTest, InterleavedReadAndWriteFromAccessList) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = 0x100, .value = 0x42, .write = true},
      {.address = 0x104, .value = 0x43, .size = MockMmioRange::Size::k16},
      {.address = 0x106, .value = 0x44, .write = true, .size = MockMmioRange::Size::k8},
      {.address = 0x108, .value = 0x45, .size = MockMmioRange::Size::k64},
  }));

  mmio_buffer_.Write32(0x42, 0x100);
  EXPECT_EQ(0x43u, mmio_buffer_.Read16(0x104));
  mmio_buffer_.Write8(0x44, 0x106);
  EXPECT_EQ(0x45u, mmio_buffer_.Read64(0x108));
}

TEST_F(MockMmioRangeTest, CheckAllAccessesReplayedIncomplete) {
  MockMmioRange mmio_range(0x1000, MockMmioRange::Size::k32);
  fdf::MmioBuffer mmio_buffer = mmio_range.GetMmioBuffer();

  mmio_range.Expect({.address = 0x100, .value = 0x42});
  mmio_range.Expect({.address = 0x100, .value = 0x43});

  EXPECT_EQ(0x42u, mmio_buffer.Read32(0x100));
  EXPECT_NONFATAL_FAILURE(mmio_range.CheckAllAccessesReplayed(), "access_list_.size()");
}

}  // namespace

}  // namespace i915_tgl
