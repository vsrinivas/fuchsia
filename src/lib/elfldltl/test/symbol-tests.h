// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_TEST_SYMBOL_TESTS_H_
#define SRC_LIB_ELFLDLTL_TEST_SYMBOL_TESTS_H_

#include <lib/elfldltl/symbol.h>

#include <string>
#include <string_view>
#include <vector>

#include "tests.h"

using namespace std::string_view_literals;

template <class Elf>
class TestSymtab {
 public:
  using Addr = typename Elf::Addr;
  using Half = typename Elf::Half;
  using Sym = typename Elf::Sym;

  uint32_t AddString(std::string_view str) {
    if (str.empty()) {
      return 0;
    }
    size_t offset = strtab_.size();
    strtab_ += str;
    strtab_ += '\0';
    return static_cast<uint32_t>(offset);
  }

  TestSymtab& AddSymbol(std::string_view name, Addr value, Addr size, elfldltl::ElfSymBind bind,
                        elfldltl::ElfSymType type, Half shndx) {
    Sym sym{};
    sym.name = AddString(name);
    sym.value = value;
    sym.size = size;
    sym.info = static_cast<uint8_t>((static_cast<uint8_t>(bind) << 4) |  //
                                    (static_cast<uint8_t>(type) << 0));
    sym.shndx = shndx;
    symtab_.push_back(sym);
    return *this;
  }

  void SetInfo(elfldltl::SymbolInfo<Elf>& si) const {
    si.set_symtab(symtab());
    si.set_strtab(strtab());
  }

  cpp20::span<const Sym> symtab() const { return {symtab_.data(), symtab_.size()}; }

  std::string_view strtab() const { return strtab_; }

 private:
  std::vector<Sym> symtab_{{}};
  std::string strtab_{'\0', 1};
};

inline constexpr std::string_view kFoobar = "foobar";
inline constexpr elfldltl::SymbolName kFoobarSymbol(kFoobar);

inline constexpr elfldltl::SymbolName kQuuxSymbol("quux"sv);
inline constexpr elfldltl::SymbolName kFooSymbol("foo"sv);
inline constexpr elfldltl::SymbolName kBarSymbol("bar"sv);
inline constexpr elfldltl::SymbolName kNotFoundSymbol("NotFound"sv);

template <class Elf>
inline const auto kTestSymbols =
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
inline constexpr Word kTestCompatHash[] = {
    0x00000005, 0x00000005, 0x00000000, 0x00000000, 0x00000001, 0x00000004,
    0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x00000002, 0x00000000,
};

// 32-bit DT_GNU_HASH data looks the same after byte-swapping.
template <typename Addr>
inline constexpr Addr kTestGnuHash[] = {
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
inline constexpr auto kTestGnuHash<Addr64LE> = MakeTestGnuHash64<Addr64LE>();

template <>
inline constexpr auto kTestGnuHash<Addr64BE> = MakeTestGnuHash64<Addr64BE>();

#endif  // SRC_LIB_ELFLDLTL_TEST_SYMBOL_TESTS_H_
