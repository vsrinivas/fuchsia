// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/coresight/rom_table.h>
#include <hwreg/mock.h>
#include <zxtest/zxtest.h>

#define EXPECT_IS_OK(result) \
  EXPECT_TRUE(result.is_ok(), "unexpected error: %s", result.error_value().data())

namespace {

constexpr uint32_t kClass0x1ComponentIDReg = 0x00000010;
constexpr uint32_t kClass0x9ComponentIDReg = 0x00000090;
constexpr uint32_t kTimestampGeneratorComponentIDReg = 0x000000f0;

constexpr uint32_t kEmptyPeriphID0Reg = 0x00000000;
constexpr uint32_t kEmptyPeriphID1Reg = 0x00000000;
constexpr uint32_t kTimestampGeneratorPeriphID0Reg = 0x00000001;
constexpr uint32_t kTimestampGeneratorPeriphID1Reg = 0x00000001;

constexpr uint32_t kDevIDReg = 0x00000000;

constexpr uint32_t kEmptyDevArchReg = 0x00000000;
constexpr uint32_t kClass0x9ROMTableDevArchReg = 0x47600af7;

constexpr uint32_t kEmptyROMEntryReg = 0x00000000;
constexpr uint32_t kOffset0x1000Class0x1ROMEntryReg = 0x00001001;
constexpr uint32_t kOffset0x2000Class0x9ROMEntryReg = 0x00002011;
constexpr uint32_t kOffset0x2000NotPresentClass0x1ROMEntryReg = 0x00002000;
constexpr uint32_t kOffset0x3000Class0x1ROMEntryReg = 0x00003001;
constexpr uint32_t kOffset0x4000Class0x1ROMEntryReg = 0x00004001;
constexpr uint32_t kOffset0x5000Class0x1ROMEntryReg = 0x00005001;
constexpr uint32_t kOffset0xa000Class0x1ROMEntryReg = 0x0000a001;
constexpr uint32_t kOffset0xfffff000Class0x1ROMEntryReg = 0xfffff001;

class ROMTableTest : public zxtest::Test {
 public:
  void TearDown() final { mock_.VerifyAndClear(); }

  hwreg::Mock& mock() { return mock_; }
  hwreg::Mock::RegisterIo& io() { return *(mock_.io()); }

  // Points at garbage, and so should not be casted and dereferenced.
  uintptr_t base_addr() { return reinterpret_cast<uintptr_t>(&dummy_base_); }

 private:
  hwreg::Mock mock_;
  uint8_t dummy_base_;
};

TEST_F(ROMTableTest, Empty0x1Table) {
  const uint32_t end_offset = 0x0000 + coresight::kMinimumComponentSize;

  mock()
      // Visit: Table (class 0x1)
      .ExpectRead(kClass0x1ComponentIDReg, 0x0000 + 0xff4)
      .ExpectRead(kEmptyDevArchReg, 0x0000 + 0xfbc)
      .ExpectRead(kDevIDReg, 0x0000 + 0xfcc)
      // Read: Entry0 of Table (empty and last)
      .ExpectRead(kEmptyROMEntryReg, 0u);

  coresight::ROMTable table(base_addr(), end_offset);
  auto result = table.Walk(io(), [base_addr = base_addr()](uintptr_t component) {
    EXPECT_TRUE(false, "unexpected component found at offset %zu", component - base_addr);
  });
  EXPECT_IS_OK(result);
}

TEST_F(ROMTableTest, Empty0x9Table) {
  const uint32_t end_offset = 0x0000 + coresight::kMinimumComponentSize;

  mock()
      // Visit: Table (class 0x9)
      .ExpectRead(kClass0x9ComponentIDReg, 0x0000 + 0xff4)
      .ExpectRead(kClass0x9ROMTableDevArchReg, 0x0000 + 0xfbc)
      .ExpectRead(kDevIDReg, 0x0000 + 0xfcc)
      // Read: Entry0 of Table (empty and last)
      .ExpectRead(kEmptyROMEntryReg, 0u);

  coresight::ROMTable table(base_addr(), end_offset);
  auto result = table.Walk(io(), [base_addr = base_addr()](uintptr_t component) {
    EXPECT_TRUE(false, "unexpected component found at offset %zu", component - base_addr);
  });
  EXPECT_IS_OK(result);
}

TEST_F(ROMTableTest, TimestampGeneratorIsVisited) {
  // clang-format off
  mock()
      // Visit: Table (class 0x1)
      .ExpectRead(kClass0x1ComponentIDReg, 0x0000 + 0xff4)
      .ExpectRead(kEmptyDevArchReg, 0x0000 + 0xfbc)
      .ExpectRead(kDevIDReg, 0x0000 + 0xfcc)
      // Read: Entry0 of Table -> Component0
      .ExpectRead(kOffset0x1000Class0x1ROMEntryReg, 0x0000)
          // Visit: Component0
          .ExpectRead(kTimestampGeneratorComponentIDReg, 0x1000 + 0xff4)
          .ExpectRead(kEmptyDevArchReg, 0x1000 + 0xfbc)
          .ExpectRead(kTimestampGeneratorPeriphID0Reg, 0x1000 + 0xfe0)
          .ExpectRead(kTimestampGeneratorPeriphID1Reg, 0x1000 + 0xfe4)
      // Read: Entry1 of Table (empty and last).
      .ExpectRead(kEmptyROMEntryReg, 0x0000 + 1*sizeof(uint32_t));
  // clang-format on

  const uint32_t end_offset = 0x1000 + coresight::kMinimumComponentSize;
  coresight::ROMTable table(base_addr(), end_offset);
  auto result = table.Walk(io(), [&, ind = size_t{0}](uintptr_t component) mutable {
    uint32_t offset = component - base_addr();
    switch (ind++) {
      case 0:
        EXPECT_EQ(0x1000, offset);
        break;
      default:
        EXPECT_TRUE(false, "unexpected component found at offset %u", offset);
    }
  });
  EXPECT_IS_OK(result);
}

TEST_F(ROMTableTest, DepthOneReferences) {
  // clang-format off
  mock()
      // Visit: Table (class 0x1)
      .ExpectRead(kClass0x1ComponentIDReg, 0x0000 + 0xff4)
      .ExpectRead(kEmptyDevArchReg, 0x0000 + 0xfbc)
      .ExpectRead(kDevIDReg, 0x0000 + 0xfcc)
      // Read: Entry0 of Table -> Component0
      .ExpectRead(kOffset0x1000Class0x1ROMEntryReg, 0x0000)
          // Visit: Component0
          .ExpectRead(kClass0x9ComponentIDReg, 0x1000 + 0xff4)
          .ExpectRead(kEmptyDevArchReg, 0x1000 + 0xfbc)
          .ExpectRead(kEmptyPeriphID0Reg, 0x1000 + 0xfe0)
          .ExpectRead(kEmptyPeriphID1Reg, 0x1000 + 0xfe4)
      // Read: Entry1 of Table (not present)
      .ExpectRead(kOffset0x2000NotPresentClass0x1ROMEntryReg, 0x0000 + 1*sizeof(uint32_t))
      // Read: Entry2 of Table -> Component2
      .ExpectRead(kOffset0x3000Class0x1ROMEntryReg, 0x0000 + 2*sizeof(uint32_t))
          // Visit: Component2
          .ExpectRead(kClass0x9ComponentIDReg, 0x3000 + 0xff4)
          .ExpectRead(kEmptyDevArchReg, 0x3000 + 0xfbc)
          .ExpectRead(kEmptyPeriphID0Reg, 0x3000 + 0xfe0)
          .ExpectRead(kEmptyPeriphID1Reg, 0x3000 + 0xfe4)
      // Read: Entry3 of Table (empty and last).
      .ExpectRead(kEmptyROMEntryReg, 0x0000 + 3*sizeof(uint32_t));
  // clang-format on

  const uint32_t end_offset = 0x3000 + coresight::kMinimumComponentSize;
  coresight::ROMTable table(base_addr(), end_offset);
  auto result = table.Walk(io(), [&, ind = size_t{0}](uintptr_t component) mutable {
    uint32_t offset = component - base_addr();
    switch (ind++) {
      case 0:
        EXPECT_EQ(0x1000, offset);
        break;
      case 1:
        EXPECT_EQ(0x3000, offset);
        break;
      default:
        EXPECT_TRUE(false, "unexpected component found at offset %u", offset);
    }
  });
  EXPECT_IS_OK(result);
}

TEST_F(ROMTableTest, DepthTwoReferences) {
  // clang-format off
  mock()
      // Visit: Table (class 0x1)
      .ExpectRead(kClass0x1ComponentIDReg, 0x0000 + 0xff4)
      .ExpectRead(kEmptyDevArchReg, 0x0000 + 0xfbc)
      .ExpectRead(kDevIDReg, 0x0000 + 0xfcc)
      // Read: Entry0 of Table -> Subtable0
      .ExpectRead(kOffset0x1000Class0x1ROMEntryReg, 0x0000)
          // Visit: Subtable0 (class 0x1)
          .ExpectRead(kClass0x1ComponentIDReg, 0x1000 + 0xff4)
          .ExpectRead(kEmptyDevArchReg, 0x1000 + 0xfbc)
          .ExpectRead(kDevIDReg, 0x1000 + 0xfcc)
          // Read: Entry0 of Subtable0 -> Component00
          .ExpectRead(kOffset0x1000Class0x1ROMEntryReg, 0x1000)
              // Visit: Component00
              .ExpectRead(kClass0x9ComponentIDReg, 0x1000 + 0x1000 + 0xff4)
              .ExpectRead(kEmptyDevArchReg, 0x1000 + 0x1000 + 0xfbc)
              .ExpectRead(kEmptyPeriphID0Reg, 0x1000 + 0x1000 + 0xfe0)
              .ExpectRead(kEmptyPeriphID1Reg, 0x1000 + 0x1000 + 0xfe4)
          // Read: Entry1 of Subtable0 (not present)
          .ExpectRead(kOffset0x2000NotPresentClass0x1ROMEntryReg, 0x1000 + 1*sizeof(uint32_t))
          // Read: Entry2 of Subtable0 -> Component02
          .ExpectRead(kOffset0x3000Class0x1ROMEntryReg, 0x1000 + 2*sizeof(uint32_t))
              // Visit: Component02
              .ExpectRead(kClass0x9ComponentIDReg, 0x1000 + 0x3000 + 0xff4)
              .ExpectRead(kEmptyDevArchReg, 0x1000 + 0x3000 + 0xfbc)
              .ExpectRead(kEmptyPeriphID0Reg, 0x1000 + 0x3000 + 0xfe0)
              .ExpectRead(kEmptyPeriphID1Reg, 0x1000 + 0x3000 + 0xfe4)
          // Read: Entry3 of Subtable0 (empty and last).
          .ExpectRead(kEmptyROMEntryReg, 0x1000 + 3*sizeof(uint32_t))
      // Read: Entry1 of Table -> Component1
      .ExpectRead(kOffset0x5000Class0x1ROMEntryReg, 0x0000 + 1*sizeof(uint32_t))
          // Visit: Component1
          .ExpectRead(kClass0x9ComponentIDReg, 0x5000 + 0xff4)
          .ExpectRead(kEmptyDevArchReg, 0x5000 + 0xfbc)
          .ExpectRead(kEmptyPeriphID0Reg, 0x5000 + 0xfe0)
          .ExpectRead(kEmptyPeriphID1Reg, 0x5000 + 0xfe4)
      // Read: Entry2 of Table -> Subtable2
      .ExpectRead(kOffset0x4000Class0x1ROMEntryReg, 0x0000 + 2*sizeof(uint32_t))
          // Visit: Subtable2 (class 0x9)
          .ExpectRead(kClass0x9ComponentIDReg, 0x4000 + 0xff4)
          .ExpectRead(kClass0x9ROMTableDevArchReg, 0x4000 + 0xfbc)
          .ExpectRead(kDevIDReg, 0x4000 + 0xfcc)
          // Read: Entry0 of Subtable2 -> Component20
          .ExpectRead(kOffset0x2000Class0x9ROMEntryReg, 0x4000)
              // Visit: Component2
              .ExpectRead(kClass0x9ComponentIDReg, 0x4000 + 0x2000 + 0xff4)
              .ExpectRead(kEmptyDevArchReg, 0x4000 + 0x2000 + 0xfbc)
              .ExpectRead(kEmptyPeriphID0Reg, 0x4000 + 0x2000 + 0xfe0)
              .ExpectRead(kEmptyPeriphID1Reg, 0x4000 + 0x2000 + 0xfe4)
          // Read: Entry1 of Subtable2 (empty and last)
          .ExpectRead(kEmptyROMEntryReg, 0x4000 + 1*sizeof(uint32_t))
      // Read: Entry3 of Table (empty and last).
      .ExpectRead(kEmptyROMEntryReg, 3*sizeof(uint32_t));
  // clang-format on

  const uint32_t end_offset = 0x6000 + coresight::kMinimumComponentSize;
  coresight::ROMTable table(base_addr(), end_offset);
  auto result = table.Walk(io(), [&, ind = size_t{0}](uintptr_t component) mutable {
    uint32_t offset = component - base_addr();
    switch (ind++) {
      case 0:
        EXPECT_EQ(0x2000, offset);
        break;
      case 1:
        EXPECT_EQ(0x4000, offset);
        break;
      case 2:
        EXPECT_EQ(0x5000, offset);
        break;
      case 3:
        EXPECT_EQ(0x6000, offset);
        break;
      default:
        EXPECT_TRUE(false, "unexpected component found at offset %u", offset);
    }
  });
  EXPECT_IS_OK(result);
}

TEST_F(ROMTableTest, NegativeOffset) {
  // An encoded -4096 in two's complement form.
  // const uint32_t comp00_encoded_offset = ~1u + 1;

  // clang-format off
  mock()
      // Visit: Table (class 0x1)
      .ExpectRead(kClass0x1ComponentIDReg, 0x0000 + 0xff4)
      .ExpectRead(kEmptyDevArchReg, 0x0000 + 0xfbc)
      .ExpectRead(kDevIDReg, 0x0000 + 0xfcc)
      // Read: Entry0 of Table -> Subtable0
      .ExpectRead(kOffset0xa000Class0x1ROMEntryReg, 0x0000)
          // Visit: Subtable0 (class 0x1)
          .ExpectRead(kClass0x1ComponentIDReg, 0xa000 + 0xff4)
          .ExpectRead(kEmptyDevArchReg, 0xa000 + 0xfbc)
          .ExpectRead(kDevIDReg, 0xa000 + 0xfcc)
          // Read: Entry0 of Subtable0 -> Component00
          .ExpectRead(kOffset0xfffff000Class0x1ROMEntryReg, 0xa000)
              // Visit: Component00
              .ExpectRead(kClass0x9ComponentIDReg, 0x9000 + 0xff4)
              .ExpectRead(kEmptyDevArchReg, 0x9000 + 0xfbc)
              .ExpectRead(kEmptyPeriphID0Reg, 0x9000 + 0xfe0)
              .ExpectRead(kEmptyPeriphID1Reg, 0x9000 + 0xfe4)
          // Read: Entry1 of Subtable0 (empty and last).
          .ExpectRead(kEmptyROMEntryReg, 0xa000 + 1*sizeof(uint32_t))
      // Read: Entry1 of Table (empty and last).
      .ExpectRead(kEmptyROMEntryReg, 1*sizeof(uint32_t));
  // clang-format on

  const uint32_t end_offset = 0xa000 + coresight::kMinimumComponentSize;
  coresight::ROMTable table(base_addr(), end_offset);
  auto result = table.Walk(io(), [&, ind = size_t{0}](uintptr_t component) mutable {
    uint32_t offset = component - base_addr();
    switch (ind++) {
      case 0:
        EXPECT_EQ(0x9000, offset);
        break;
      default:
        EXPECT_TRUE(false, "unexpected component found at offset %u", offset);
    }
  });
  EXPECT_IS_OK(result);
}

}  // namespace
