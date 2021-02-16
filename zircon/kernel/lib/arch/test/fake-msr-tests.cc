// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-msr.h>

#include <optional>

#include <gtest/gtest.h>

namespace {

TEST(FakeMsrIoTests, PopulateAndPeek) {
  arch::testing::FakeMsrIo io;
  io.Populate(arch::X86Msr::IA32_FS_BASE, 0xaaaa'bbbb'cccc'dddd);
  EXPECT_EQ(0xaaaa'bbbb'cccc'ddddu, io.Peek(arch::X86Msr::IA32_FS_BASE));
  EXPECT_EQ(0xaaaa'bbbb'cccc'ddddu, io.Peek(arch::X86Msr::IA32_FS_BASE));
}

TEST(FakeMsrIoTests, Read) {
  const uint32_t fs_base = static_cast<uint32_t>(arch::X86Msr::IA32_FS_BASE);

  arch::testing::FakeMsrIo io;
  io.Populate(arch::X86Msr::IA32_FS_BASE, 0xaaaa'bbbb'cccc'dddd);
  EXPECT_EQ(0xaaaa'bbbb'cccc'ddddu, io.Read<uint64_t>(fs_base));
  EXPECT_EQ(0xaaaa'bbbb'cccc'ddddu, io.Read<uint64_t>(fs_base));
  EXPECT_EQ(0xaaaa'bbbb'cccc'ddddu, io.Peek(arch::X86Msr::IA32_FS_BASE));
}

TEST(FakeMsrIoTests, PopulateOverwrites) {
  arch::testing::FakeMsrIo io;
  io.Populate(arch::X86Msr::IA32_FS_BASE, 0xaaaa'bbbb'cccc'dddd);
  EXPECT_EQ(0xaaaa'bbbb'cccc'ddddu, io.Peek(arch::X86Msr::IA32_FS_BASE));

  io.Populate(arch::X86Msr::IA32_FS_BASE, 0xdddd'cccc'bbbb'aaaa);
  EXPECT_EQ(0xdddd'cccc'bbbb'aaaau, io.Peek(arch::X86Msr::IA32_FS_BASE));
}

TEST(FakeMsrIoTests, Write) {
  const uint32_t fs_base = static_cast<uint32_t>(arch::X86Msr::IA32_FS_BASE);

  arch::testing::FakeMsrIo io;
  io.Populate(arch::X86Msr::IA32_FS_BASE, 0xaaaa'bbbb'cccc'dddd);

  io.Write(uint64_t{0xdddd'cccc'bbbb'aaaa}, fs_base);
  EXPECT_EQ(0xdddd'cccc'bbbb'aaaau, io.Peek(arch::X86Msr::IA32_FS_BASE));
  EXPECT_EQ(0xdddd'cccc'bbbb'aaaau, io.Read<uint64_t>(fs_base));

  io.Write(uint64_t{0xaaaa'bbbb'cccc'dddd}, fs_base);
  EXPECT_EQ(0xaaaa'bbbb'cccc'ddddu, io.Peek(arch::X86Msr::IA32_FS_BASE));
  EXPECT_EQ(0xaaaa'bbbb'cccc'ddddu, io.Read<uint64_t>(fs_base));
}

TEST(FakeMsrIoTests, IoWithSideEffects) {
  const uint32_t fs_base = static_cast<uint32_t>(arch::X86Msr::IA32_FS_BASE);
  const uint32_t gs_base = static_cast<uint32_t>(arch::X86Msr::IA32_GS_BASE);

  std::optional<arch::X86Msr> last_written_msr, last_read_msr;
  std::optional<uint64_t> last_written_value, last_read_value;

  // These are of course nonsense side-effects.
  auto on_write = [&last_written_msr, &last_written_value](arch::X86Msr msr, uint64_t& value) {
    last_written_msr = msr;
    last_written_value = value;
    value = 0x1234'1234'1234'1234;  // Reset to a strange, specific value.
  };
  auto on_read = [&last_read_msr, &last_read_value](arch::X86Msr msr, uint64_t& value) {
    last_read_msr = msr;
    last_read_value = value;
  };

  arch::testing::FakeMsrIo io(std::move(on_write), std::move(on_read));

  // Populate should have no side-effects - and can be chained.
  io.Populate(arch::X86Msr::IA32_FS_BASE, 0xaaaa'bbbb'cccc'dddd)
      .Populate(arch::X86Msr::IA32_GS_BASE, 0xabcd'abcd'abcd'abcd);
  EXPECT_FALSE(last_read_msr.has_value());
  EXPECT_FALSE(last_read_value.has_value());
  EXPECT_FALSE(last_written_msr.has_value());
  EXPECT_FALSE(last_written_value.has_value());

  // Peek should have no side-effects.
  static_cast<void>(io.Peek(arch::X86Msr::IA32_FS_BASE));
  static_cast<void>(io.Peek(arch::X86Msr::IA32_GS_BASE));
  EXPECT_FALSE(last_read_msr.has_value());
  EXPECT_FALSE(last_read_value.has_value());
  EXPECT_FALSE(last_written_msr.has_value());
  EXPECT_FALSE(last_written_value.has_value());

  // Read should only update the last_read_* variables to not alter the stored
  // register value.
  EXPECT_EQ(0xaaaa'bbbb'cccc'ddddu, io.Read<uint64_t>(fs_base));
  EXPECT_EQ(0xaaaa'bbbb'cccc'ddddu, io.Read<uint64_t>(fs_base));
  EXPECT_EQ(arch::X86Msr::IA32_FS_BASE, *last_read_msr);
  EXPECT_EQ(0xaaaa'bbbb'cccc'ddddu, *last_read_value);
  EXPECT_FALSE(last_written_msr.has_value());
  EXPECT_FALSE(last_written_value.has_value());

  EXPECT_EQ(0xabcd'abcd'abcd'abcdu, io.Read<uint64_t>(gs_base));
  EXPECT_EQ(0xabcd'abcd'abcd'abcdu, io.Read<uint64_t>(gs_base));
  EXPECT_EQ(arch::X86Msr::IA32_GS_BASE, *last_read_msr);
  EXPECT_EQ(0xabcd'abcd'abcd'abcdu, *last_read_value);
  EXPECT_FALSE(last_written_msr.has_value());
  EXPECT_FALSE(last_written_value.has_value());

  // Write should only update the last_written_* variables and reset the stored
  // register value to 0x1234'1234'1234'1234.
  last_read_msr = std::nullopt;
  last_read_value = std::nullopt;
  io.Write(uint64_t{0xdddd'cccc'bbbb'aaaa}, fs_base);
  EXPECT_FALSE(last_read_msr.has_value());
  EXPECT_FALSE(last_read_value.has_value());
  EXPECT_EQ(arch::X86Msr::IA32_FS_BASE, *last_written_msr);
  EXPECT_EQ(0xdddd'cccc'bbbb'aaaau, *last_written_value);
  EXPECT_EQ(0x1234'1234'1234'1234u, io.Peek(arch::X86Msr::IA32_FS_BASE));

  last_read_msr = std::nullopt;
  last_read_value = std::nullopt;
  io.Write(uint64_t{0xdcba'dcba'dcba'dcba}, gs_base);
  EXPECT_FALSE(last_read_msr.has_value());
  EXPECT_FALSE(last_read_value.has_value());
  EXPECT_EQ(arch::X86Msr::IA32_GS_BASE, *last_written_msr);
  EXPECT_EQ(0xdcba'dcba'dcba'dcbau, *last_written_value);
  EXPECT_EQ(0x1234'1234'1234'1234u, io.Peek(arch::X86Msr::IA32_GS_BASE));
}

}  // namespace
