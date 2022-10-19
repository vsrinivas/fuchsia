// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "symbol-tests.h"

#include <array>
#include <set>
#include <vector>

namespace {

constexpr std::string_view kEmpty{};
constexpr elfldltl::SymbolName kEmptySymbol(kEmpty);
constexpr uint32_t kEmptyCompatHash = 0;
constexpr uint32_t kEmptyGnuHash = 5381;

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

constexpr auto CompatHashSize = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;

  elfldltl::SymbolInfo<Elf> si;
  kTestSymbols<Elf>.SetInfo(si);
  si.set_compat_hash(kTestCompatHash<typename Elf::Word>);

  EXPECT_EQ(si.safe_symtab().size(), kTestSymbolCount);
};

TEST(ElfldltlSymbolTests, CompatHashSize) {
  ASSERT_NO_FATAL_FAILURE(TestAllFormats(CompatHashSize));
}

constexpr auto GnuHashSize = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;

  elfldltl::SymbolInfo<Elf> si;
  kTestSymbols<Elf>.SetInfo(si);
  si.set_gnu_hash(kTestGnuHash<typename Elf::Addr>);

  EXPECT_EQ(si.safe_symtab().size(), kTestSymbolCount);
};

TEST(ElfldltlSymbolTests, GnuHashSize) { ASSERT_NO_FATAL_FAILURE(TestAllFormats(GnuHashSize)); }

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
  ASSERT_NO_FATAL_FAILURE(TestAllFormats(LookupCompatHash));
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

TEST(ElfldltlSymbolTests, LookupGnuHash) { ASSERT_NO_FATAL_FAILURE(TestAllFormats(LookupGnuHash)); }

// The enumeration tests use the same symbol table with both flavors of hash
// table.

template <class Elf>
struct CompatHash {
  using Table = typename elfldltl::CompatHash<typename Elf::Word>;
  static Table Get(const elfldltl::SymbolInfo<Elf>& si) { return *si.compat_hash(); }
  static constexpr std::string_view kNames[] = {
      "bar",
      "foo",
      "foobar",
      "quux",
  };
};

template <class Elf>
struct GnuHash {
  using Table = typename elfldltl::GnuHash<typename Elf::Word, typename Elf::Addr>;
  static Table Get(const elfldltl::SymbolInfo<Elf>& si) { return *si.gnu_hash(); }
  static constexpr std::string_view kNames[] = {
      // The DT_GNU_HASH table omits the undefined symbols.
      "bar",
      "foo",
      "foobar",
  };
};

template <template <class Elf> class HashTable>
constexpr auto EnumerateHashTable = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using HashBucket =
      typename elfldltl::SymbolInfo<Elf>::template HashBucket<typename HashTable<Elf>::Table>;

  elfldltl::SymbolInfo<Elf> si;
  kTestSymbols<Elf>.SetInfo(si);
  si.set_compat_hash(kTestCompatHash<typename Elf::Word>);
  si.set_gnu_hash(kTestGnuHash<typename Elf::Addr>);
  const auto hash_table = HashTable<Elf>::Get(si);

  // Collect all the symbols in a sorted set that doesn't remove duplicates.
  std::multiset<std::string_view> symbol_names;
  for (auto bucket : hash_table) {
    for (uint32_t symndx : HashBucket(hash_table, bucket)) {
      const auto& sym = si.symtab()[symndx];
      std::string_view name = si.string(sym.name);
      ASSERT_FALSE(name.empty());
      symbol_names.insert(name);
    }
  }

  std::vector<std::string_view> sorted_names(symbol_names.begin(), symbol_names.end());
  ASSERT_EQ(sorted_names.size(), std::size(HashTable<Elf>::kNames));
  for (size_t i = 0; i < sorted_names.size(); ++i) {
    EXPECT_EQ(sorted_names[i], HashTable<Elf>::kNames[i]);
  }
};

TEST(ElfldltlSymbolTests, EnumerateCompatHash) {
  ASSERT_NO_FATAL_FAILURE(TestAllFormats(EnumerateHashTable<CompatHash>));
}

TEST(ElfldltlSymbolTests, EnumerateGnuHash) {
  ASSERT_NO_FATAL_FAILURE(TestAllFormats(EnumerateHashTable<GnuHash>));
}

}  // namespace
