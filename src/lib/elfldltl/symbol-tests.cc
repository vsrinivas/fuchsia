// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "symbol-tests.h"

#include <array>

namespace {

using namespace std::literals;

constexpr std::string_view kEmpty{};
constexpr elfldltl::SymbolName kEmptySymbol(kEmpty);
constexpr uint32_t kEmptyCompatHash = 0;
constexpr uint32_t kEmptyGnuHash = 5381;

constexpr std::string_view kFoobar = "foobar";
constexpr elfldltl::SymbolName kFoobarSymbol(kFoobar);
constexpr uint32_t kFoobarCompatHash = 0x06d65882;
constexpr uint32_t kFoobarGnuHash = 0xfde460be;

TEST(ElfldltlSymbolTests, CompatHash) {
  EXPECT_EQ(kEmptyCompatHash, elfldltl::SymbolName(kEmpty).compat_hash());
  EXPECT_EQ(kFoobarCompatHash, elfldltl::SymbolName(kFoobar).compat_hash());
}

static_assert(kEmptySymbol.compat_hash() == kEmptyCompatHash);
static_assert(kFoobarSymbol.compat_hash() == kFoobarCompatHash);

TEST(ElfldltlSymbolTests, GnuHash) {
  EXPECT_EQ(kEmptyGnuHash, elfldltl::SymbolName(kEmpty).gnu_hash());
  EXPECT_EQ(kFoobarGnuHash, elfldltl::SymbolName(kFoobar).gnu_hash());
}

static_assert(kEmptySymbol.gnu_hash() == kEmptyGnuHash);
static_assert(kFoobarSymbol.gnu_hash() == kFoobarGnuHash);

constexpr elfldltl::SymbolName kQuuxSymbol("quux"sv);
constexpr elfldltl::SymbolName kFooSymbol("foo"sv);
constexpr elfldltl::SymbolName kBarSymbol("bar"sv);
constexpr elfldltl::SymbolName kNotFoundSymbol("NotFound"sv);

template <class Elf>
const auto kTestSymbols =
    TestSymtab<Elf>()
        .AddSymbol(kQuuxSymbol, 0, 0, elfldltl::ElfSymBind::kGlobal, elfldltl::ElfSymType::kFunc, 0)
        .AddSymbol(kFooSymbol, 1, 1, elfldltl::ElfSymBind::kGlobal, elfldltl::ElfSymType::kFunc, 1)
        .AddSymbol(kBarSymbol, 2, 1, elfldltl::ElfSymBind::kGlobal, elfldltl::ElfSymType::kFunc, 1)
        .AddSymbol(kFoobarSymbol, 3, 1, elfldltl::ElfSymBind::kGlobal, elfldltl::ElfSymType::kFunc,
                   1);

// There is always a null entry at index 0, which is counted in the size.
constexpr size_t kTestSymbolCount = 5;

// DT_HASH data is always in the same format, modulo byte-swapping.
template <typename Word>
constexpr Word kTestCompatHash[] = {
    0x00000005, 0x00000005, 0x00000000, 0x00000000, 0x00000001, 0x00000004,
    0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x00000002, 0x00000000,
};

// 32-bit DT_GNU_HASH data looks the same after byte-swapping.
template <typename Addr>
constexpr Addr kTestGnuHash[] = {
    0x00000001, 0x00000002, 0x00000002, 0x0000001a, 0x00000204,
    0xc4000004, 0x00000002, 0x0b887388, 0x0b8860ba, 0xfde460bf,
};

// The 64-bit data isn't just byte-swapped, since some 64-bit words are
// actually pairs of 32-bit words and their relative order isn't swapped.
using Addr64BE = elfldltl::Elf64<elfldltl::ElfData::k2Msb>::Addr;
using Addr64LE = elfldltl::Elf64<elfldltl::ElfData::k2Lsb>::Addr;

template <typename Addr>
constexpr Addr WordPair(uint32_t first, uint32_t second) {
  if constexpr (std::is_same_v<Addr, Addr64BE>) {
    return (static_cast<uint64_t>(first) << 32) | second;
  } else {
    static_assert(std::is_same_v<Addr, Addr64LE>);
    return (static_cast<uint64_t>(second) << 32) | first;
  }
}

template <typename Addr>
constexpr auto MakeTestGnuHash64() {
  return std::array{
      WordPair<Addr>(0x00000001, 0x00000002),  // nbucket, bias
      WordPair<Addr>(0x00000001, 0x0000001a),  // nfilter, shift
      Addr{0xc400000000000204},                // Bloom filter words (64-bit)
      WordPair<Addr>(0x00000002, 0x0b887388),  // sole hash bucket, and ...
      WordPair<Addr>(0x0b8860ba, 0xfde460bf),  // chain table words
  };
}

template <>
constexpr auto kTestGnuHash<Addr64LE> = MakeTestGnuHash64<Addr64LE>();

template <>
constexpr auto kTestGnuHash<Addr64BE> = MakeTestGnuHash64<Addr64BE>();

constexpr auto CompatHashSize = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;

  elfldltl::SymbolInfo<Elf> si;
  kTestSymbols<Elf>.SetInfo(si);
  si.set_compat_hash(kTestCompatHash<typename Elf::Word>);

  EXPECT_EQ(si.safe_symtab().size(), kTestSymbolCount);
};

TEST(ElfldltlSymbolTests, CompatHashSize) {
  ASSERT_NO_FATAL_FAILURES(TestAllFormats(CompatHashSize));
}

constexpr auto GnuHashSize = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;

  elfldltl::SymbolInfo<Elf> si;
  kTestSymbols<Elf>.SetInfo(si);
  si.set_gnu_hash(kTestGnuHash<typename Elf::Addr>);

  EXPECT_EQ(si.safe_symtab().size(), kTestSymbolCount);
};

TEST(ElfldltlSymbolTests, GnuHashSize) { ASSERT_NO_FATAL_FAILURES(TestAllFormats(GnuHashSize)); }

constexpr auto LookupCompatHash = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;

  elfldltl::SymbolInfo<Elf> si;
  kTestSymbols<Elf>.SetInfo(si);
  si.set_compat_hash(kTestCompatHash<typename Elf::Word>);

  EXPECT_NULL(kNotFoundSymbol.Lookup(si));

  EXPECT_NULL(kQuuxSymbol.Lookup(si));  // Undefined should be skipped.

  const auto* foo = kFooSymbol.Lookup(si);
  ASSERT_NOT_NULL(foo);
  EXPECT_EQ(foo->value(), 1);

  const auto* bar = kBarSymbol.Lookup(si);
  ASSERT_NOT_NULL(bar);
  EXPECT_EQ(bar->value(), 2);

  const auto* foobar = kFoobarSymbol.Lookup(si);
  ASSERT_NOT_NULL(foobar);
  EXPECT_EQ(foobar->value(), 3);
};

TEST(ElfldltlSymbolTests, LookupCompatHash) {
  ASSERT_NO_FATAL_FAILURES(TestAllFormats(LookupCompatHash));
}

constexpr auto LookupGnuHash = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;

  elfldltl::SymbolInfo<Elf> si;
  kTestSymbols<Elf>.SetInfo(si);
  si.set_gnu_hash(kTestGnuHash<typename Elf::Addr>);

  EXPECT_NULL(kNotFoundSymbol.Lookup(si));

  EXPECT_NULL(kQuuxSymbol.Lookup(si));  // Undefined should be skipped.

  const auto* foo = kFooSymbol.Lookup(si);
  ASSERT_NOT_NULL(foo);
  EXPECT_EQ(foo->value(), 1);

  const auto* bar = kBarSymbol.Lookup(si);
  ASSERT_NOT_NULL(bar);
  EXPECT_EQ(bar->value(), 2);

  const auto* foobar = kFoobarSymbol.Lookup(si);
  ASSERT_NOT_NULL(foobar);
  EXPECT_EQ(foobar->value(), 3);
};

TEST(ElfldltlSymbolTests, LookupGnuHash) {
  ASSERT_NO_FATAL_FAILURES(TestAllFormats(LookupGnuHash));
}

}  // namespace
