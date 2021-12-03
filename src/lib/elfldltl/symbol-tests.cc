// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "symbol-tests.h"

namespace {

using namespace std::literals;

constexpr std::string_view kEmpty{};
constexpr elfldltl::SymbolName kEmptySymbol(kEmpty);
constexpr uint32_t kEmptyCompatHash = 0;

constexpr std::string_view kFoobar = "foobar";
constexpr elfldltl::SymbolName kFoobarSymbol(kFoobar);
constexpr uint32_t kFoobarCompatHash = 0x06d65882;

TEST(ElfldltlSymbolTests, CompatHash) {
  EXPECT_EQ(kEmptyCompatHash, elfldltl::SymbolName(kEmpty).compat_hash());
  EXPECT_EQ(kFoobarCompatHash, elfldltl::SymbolName(kFoobar).compat_hash());
}

static_assert(kEmptySymbol.compat_hash() == kEmptyCompatHash);
static_assert(kFoobarSymbol.compat_hash() == kFoobarCompatHash);

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

}  // namespace
