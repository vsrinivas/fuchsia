// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/symbolizer-markup/writer.h>

#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

struct Sink {
  void operator()(std::string_view s) { value.append(s); }

  std::string& value;
};

TEST(SymbolizerMarkupTests, Literals) {
  std::string markup;
  symbolizer_markup::Writer writer(Sink{markup});

  writer.Literal("ab").Literal('c').Newline().Literal("123");

  EXPECT_EQ("abc\n123", markup);
}

TEST(SymbolizerMarkupTests, DecimalDigits) {
  std::string markup;
  symbolizer_markup::Writer writer(Sink{markup});

  writer.DecimalDigits(0u)
      .Newline()
      .DecimalDigits(size_t{1})
      .Newline()
      .DecimalDigits(9u)
      .Newline()
      .DecimalDigits(uint8_t{10})
      .Newline()
      .DecimalDigits(uint16_t{123})
      .Newline()
      .DecimalDigits(100000000u)
      .Newline()
      .DecimalDigits(uint32_t{123454321})
      .Newline()
      .DecimalDigits(uint64_t{12345678987654321})
      .Newline()
      .DecimalDigits(9999999999999999999u);

  constexpr std::string_view kExpected = R"""(0
1
9
10
123
100000000
123454321
12345678987654321
9999999999999999999)""";
  EXPECT_EQ(kExpected, markup);
}

TEST(SymbolizerMarkupTests, HexDigits) {
  std::string markup;
  symbolizer_markup::Writer writer(Sink{markup});

  writer.HexDigits(0x0u)
      .Newline()
      .HexDigits(size_t{0x1})
      .Newline()
      .HexDigits(0xau)
      .Newline()
      .HexDigits(uint8_t{0xff})
      .Newline()
      .HexDigits(uint16_t{0xabc})
      .Newline()
      .HexDigits(0xffff'ffffu)
      .Newline()
      .HexDigits(uint32_t{0xabcdcba})
      .Newline()
      .HexDigits(uint64_t{0x1234567890abcdef})
      .Newline()
      .HexDigits(0xffff'ffff'ffff'ffffu);

  constexpr std::string_view kExpected = R"""(0x0
0x1
0xa
0xff
0xabc
0xffffffff
0xabcdcba
0x1234567890abcdef
0xffffffffffffffff)""";
  EXPECT_EQ(kExpected, markup);
}

TEST(SymbolizerMarkupTests, Colors) {
  using symbolizer_markup::Color;

  std::string markup;
  symbolizer_markup::Writer writer(Sink{markup});

#define DEFAULT "\033[0m"
#define BOLD "\033[1m"
#define BLACK "\033[30m"
#define GREEN "\033[32m"
#define MAGENTA "\033[35m"

  {
    auto magenta = writer.ChangeColor(Color::kMagenta);
    EXPECT_EQ(MAGENTA, markup);

    auto black_bold = writer.ChangeColor(Color::kBlack, /*bold=*/true);
    EXPECT_EQ(MAGENTA BLACK BOLD, markup);

    {
      auto green = writer.ChangeColor(Color::kGreen);
      EXPECT_EQ(MAGENTA BLACK BOLD GREEN, markup);
    }
    // With `green` out of scope, we should automatically change back to the
    // default color.
    EXPECT_EQ(MAGENTA BLACK BOLD GREEN DEFAULT, markup);
  }
  // With `magenta` and `black_bold` out of scope, we should automatically
  // change back to the default color twice again (for good measure).
  EXPECT_EQ(MAGENTA BLACK BOLD GREEN DEFAULT DEFAULT DEFAULT, markup);

#undef DEFAULT
#undef BOLD
#undef BLACK
#undef GREEN
#undef MAGENTA
}

TEST(SymbolizerMarkupTests, Symbol) {
  std::string markup;
  symbolizer_markup::Writer writer(Sink{markup});

  writer.Symbol("_ZN7Mangled4NameEv").Newline().Symbol("foobar");

  constexpr std::string_view kExpected = R"""({{{symbol:_ZN7Mangled4NameEv}}}
{{{symbol:foobar}}})""";
  EXPECT_EQ(kExpected, markup);
}

TEST(SymbolizerMarkupTests, Code) {
  std::string markup;
  symbolizer_markup::Writer writer(Sink{markup});

  writer.Code(uintptr_t{0xffffffff0000abcd})
      .Newline()
      .Code(uintptr_t{0x1234567800000000})
      .Newline()
      .Code(uintptr_t{0x123})
      .Newline()
      .Code(uintptr_t{0x0});

  constexpr std::string_view kExpected = R"""({{{pc:0xffffffff0000abcd}}}
{{{pc:0x1234567800000000}}}
{{{pc:0x123}}}
{{{pc:0x0}}})""";
  EXPECT_EQ(kExpected, markup);
}

TEST(SymbolizerMarkupTests, Data) {
  std::string markup;
  symbolizer_markup::Writer writer(Sink{markup});

  writer.Data(uintptr_t{0xffffffff0000abcd})
      .Newline()
      .Data(uintptr_t{0x1234567800000000})
      .Newline()
      .Data(uintptr_t{0x123})
      .Newline()
      .Data(uintptr_t{0x0});

  constexpr std::string_view kExpected = R"""({{{data:0xffffffff0000abcd}}}
{{{data:0x1234567800000000}}}
{{{data:0x123}}}
{{{data:0x0}}})""";
  EXPECT_EQ(kExpected, markup);
}

TEST(SymbolizerMarkupTests, BacktraceFrame) {
  std::string markup;
  symbolizer_markup::Writer writer(Sink{markup});

  writer.ExactPcFrame(9, uintptr_t{0xffffffff0000abcd})
      .Newline()
      .ReturnAddressFrame(10, uintptr_t{0x12345678})
      .Newline()
      .ReturnAddressFrame(11, uintptr_t{0x55555555});

  constexpr std::string_view kExpected = R"""({{{bt:9:0xffffffff0000abcd:pc}}}
{{{bt:10:0x12345678:ra}}}
{{{bt:11:0x55555555:ra}}})""";
  EXPECT_EQ(kExpected, markup);
}

TEST(SymbolizerMarkupTests, Dumpfile) {
  std::string markup;
  symbolizer_markup::Writer writer(Sink{markup});

  writer.Dumpfile("TYPE", "NAME").Newline().Dumpfile("sancov", "sancov.8675");

  constexpr std::string_view kExpected = R"""({{{dumpfile:TYPE:NAME}}}
{{{dumpfile:sancov:sancov.8675}}})""";
  EXPECT_EQ(kExpected, markup);
}

TEST(SymbolizerMarkupTests, Reset) {
  std::string markup;
  symbolizer_markup::Writer writer(Sink{markup});

  writer.Reset();
  EXPECT_EQ("{{{reset}}}", markup);
}

TEST(SymbolizerMarkupTests, Module) {
  constexpr uint8_t kBuildIdA[] = {0x54, 0x59, 0x75, 0x39, 0x4d, 0x10, 0xa0, 0x7d};
  constexpr uint8_t kBuildIdB[] = {0xba, 0x43, 0xd6, 0xf6, 0x91, 0x1e, 0x87, 0x23};

  std::string markup;
  symbolizer_markup::Writer writer(Sink{markup});

  writer.ElfModule(5, "moduleA", cpp20::as_bytes(cpp20::span{kBuildIdA}))
      .Newline()
      .ElfModule(10, "moduleB", cpp20::as_bytes(cpp20::span{kBuildIdB}));

  constexpr std::string_view kExpected = R"""({{{module:5:moduleA:elf:545975394d10a07d}}}
{{{module:10:moduleB:elf:ba43d6f6911e8723}}})""";
  EXPECT_EQ(kExpected, markup);
}

TEST(SymbolizerMarkupTests, LoadImageMmap) {
  using Perms = symbolizer_markup::MemoryPermissions;

  constexpr Perms kR = {.read = true};
  constexpr Perms kRW = {.read = true, .write = true};
  constexpr Perms kRWX = {.read = true, .write = true, .execute = true};
  constexpr Perms kRX = {.read = true, .execute = true};
  constexpr Perms kW = {.write = true};
  constexpr Perms kWX = {.write = true, .execute = true};
  constexpr Perms kX = {.execute = true};

  std::string markup;
  symbolizer_markup::Writer writer(Sink{markup});

  writer.LoadImageMmap(uintptr_t{0x1000'0000}, 0x1000, 0, kR, 0x400)
      .Newline()
      .LoadImageMmap(uintptr_t{0x2000'0000}, 0x2000, 1, kRW, 0x800)
      .Newline()
      .LoadImageMmap(uintptr_t{0x3000'0000}, 0x3000, 2, kRWX, 0xc00)
      .Newline()
      .LoadImageMmap(uintptr_t{0x4000'0000}, 0x4000, 3, kRX, 0x1000)
      .Newline()
      .LoadImageMmap(uintptr_t{0x5000'0000}, 0x5000, 4, kW, 0x1400)
      .Newline()
      .LoadImageMmap(uintptr_t{0x6000'0000}, 0x6000, 5, kWX, 0x1800)
      .Newline()
      .LoadImageMmap(uintptr_t{0x7000'0000}, 0x7000, 6, kX, 0x1c00);

  constexpr std::string_view kExpected =
      R"""({{{mmap:0x10000000:0x1000:load:0:r:0x400}}}
{{{mmap:0x20000000:0x2000:load:1:rw:0x800}}}
{{{mmap:0x30000000:0x3000:load:2:rwx:0xc00}}}
{{{mmap:0x40000000:0x4000:load:3:rx:0x1000}}}
{{{mmap:0x50000000:0x5000:load:4:w:0x1400}}}
{{{mmap:0x60000000:0x6000:load:5:wx:0x1800}}}
{{{mmap:0x70000000:0x7000:load:6:x:0x1c00}}})""";
  EXPECT_EQ(kExpected, markup);
}

}  // namespace
