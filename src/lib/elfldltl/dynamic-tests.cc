// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/dynamic.h>
#include <lib/elfldltl/memory.h>

#include <array>
#include <string>
#include <type_traits>
#include <vector>

#include <zxtest/zxtest.h>

#include "symbol-tests.h"

namespace {

template <typename Dyn, size_t N>
constexpr cpp20::span<const Dyn> DynSpan(const std::array<Dyn, N>& dyn) {
  return {dyn};
}

constexpr elfldltl::DiagnosticsFlags kDiagFlags = {.multiple_errors = true};

constexpr auto EmptyTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors);

  elfldltl::DirectMemory memory({}, 0);

  // Nothing but the terminator.
  constexpr std::array dyn{
      typename Elf::Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  // No matchers and nothing to match.
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag, memory, DynSpan(dyn)));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
};

TEST(ElfldltlDynamicTests, Empty) { TestAllFormats(EmptyTest); }

constexpr auto MissingTerminatorTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kDiagFlags);

  elfldltl::DirectMemory memory({}, 0);

  // Empty span has no terminator.
  cpp20::span<const typename Elf::Dyn> dyn;

  EXPECT_TRUE(elfldltl::DecodeDynamic(diag, memory, dyn));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_GE(errors.size(), 1);
  EXPECT_STR_EQ(errors.front(), "missing DT_NULL terminator in PT_DYNAMIC");
};

TEST(ElfldltlDynamicTests, MissingTerminator) { TestAllFormats(MissingTerminatorTest); }

constexpr auto RejectTextrelTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kDiagFlags);

  elfldltl::DirectMemory memory({}, 0);

  // PT_DYNAMIC without DT_TEXTREL.
  constexpr std::array dyn_notextrel{
      typename Elf::Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  EXPECT_TRUE(elfldltl::DecodeDynamic(diag, memory, DynSpan(dyn_notextrel),
                                      elfldltl::DynamicTextrelRejectObserver{}));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  EXPECT_TRUE(errors.empty());

  // PT_DYNAMIC with DT_TEXTREL.
  constexpr std::array dyn_textrel{
      typename Elf::Dyn{.tag = elfldltl::ElfDynTag::kTextRel},
      typename Elf::Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  EXPECT_TRUE(elfldltl::DecodeDynamic(diag, memory, DynSpan(dyn_textrel),
                                      elfldltl::DynamicTextrelRejectObserver{}));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_GE(errors.size(), 1);
  EXPECT_STR_EQ(errors.front(), elfldltl::DynamicTextrelRejectObserver::Message());
};

TEST(ElfldltlDynamicTests, RejectTextrel) { TestAllFormats(RejectTextrelTest); }

// This synthesizes a memory image of symbol-related test data with known
// offsets and addresses that can be referenced in dynamic section entries in
// the specific test data.  The same image contents are used for several tests
// below with different dynamic section data.  Because the Memory API admits
// mutation of the image, the same image buffer shouldn't be reused for
// multiple tests just in case a test mutates the buffer (though they are meant
// not to).  So this helper object is created in each test case to reconstruct
// the same data afresh.
template <typename Elf>
class SymbolInfoTestImage {
 public:
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;
  using Sym = typename Elf::Sym;

  SymbolInfoTestImage() {
    // Build up some good symbol data in a memory image.
    soname_offset_ = test_syms_.AddString("libfoo.so");

    auto symtab_bytes = cpp20::as_bytes(test_syms_.symtab());
    cpp20::span<const std::byte> strtab_bytes{
        reinterpret_cast<const std::byte*>(test_syms_.strtab().data()),
        test_syms_.strtab().size(),
    };

    image_ = std::vector<std::byte>(symtab_bytes.begin(), symtab_bytes.end());
    auto next_addr = [this]() -> size_type {
      size_t align_pad = sizeof(size_type) - (image_.size() % sizeof(size_type));
      image_.insert(image_.end(), align_pad, std::byte{});
      return kSymtabAddr + static_cast<size_type>(image_.size());
    };

    strtab_addr_ = next_addr();
    image_.insert(image_.end(), strtab_bytes.begin(), strtab_bytes.end());

    gnu_hash_addr_ = next_addr();
    auto gnu_hash_data = cpp20::span(kTestGnuHash<typename Elf::Addr>);
    auto gnu_hash_bytes = cpp20::as_bytes(gnu_hash_data);
    image_.insert(image_.end(), gnu_hash_bytes.begin(), gnu_hash_bytes.end());

    hash_addr_ = next_addr();
    auto hash_data = cpp20::span(kTestCompatHash<typename Elf::Word>);
    auto hash_bytes = cpp20::as_bytes(hash_data);
    image_.insert(image_.end(), hash_bytes.begin(), hash_bytes.end());
  }

  size_type soname_offset() const { return soname_offset_; }

  size_type strtab_addr() const { return strtab_addr_; }

  size_t strtab_size_bytes() const { return test_syms_.strtab().size(); }

  size_type symtab_addr() { return kSymtabAddr; }

  size_type hash_addr() const { return hash_addr_; }

  size_type gnu_hash_addr() const { return gnu_hash_addr_; }

  const TestSymtab<Elf>& test_syms() const { return test_syms_; }

  size_t size_bytes() const { return image_.size(); }

  elfldltl::DirectMemory memory() { return elfldltl::DirectMemory(image_, kSymtabAddr); }

 private:
  static constexpr size_type kSymtabAddr = 0x1000;

  std::vector<std::byte> image_;
  TestSymtab<Elf> test_syms_ = kTestSymbols<Elf>;
  size_type soname_offset_ = 0;
  size_type strtab_addr_ = 0;
  size_type hash_addr_ = 0;
  size_type gnu_hash_addr_ = 0;
};

class TestDiagnostics {
 public:
  using DiagType = decltype(elfldltl::CollectStringsDiagnostics(
      std::declval<std::vector<std::string>&>(), kDiagFlags));

  DiagType& diag() { return diag_; }

  const std::vector<std::string>& errors() const { return errors_; }

  std::string ExplainErrors() const {
    std::string str = std::to_string(diag_.errors()) + " errors, " +
                      std::to_string(diag_.warnings()) + " warnings:";
    for (const std::string& line : errors_) {
      str += "\n\t";
      str += line;
    }
    return str;
  }

 private:
  std::vector<std::string> errors_;
  DiagType diag_ = elfldltl::CollectStringsDiagnostics(errors_, kDiagFlags);
};

constexpr auto SymbolInfoObserverEmptyTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Dyn = typename Elf::Dyn;

  TestDiagnostics diag;
  elfldltl::DirectMemory empty_memory({}, 0);

  // PT_DYNAMIC with no symbol info.
  constexpr std::array dyn_nosyms{
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), empty_memory, DynSpan(dyn_nosyms),
                                      elfldltl::DynamicSymbolInfoObserver(info)),
              "%s", diag.ExplainErrors().c_str());

  EXPECT_EQ(0, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_TRUE(diag.errors().empty());

  EXPECT_TRUE(info.strtab().empty());
  EXPECT_TRUE(info.symtab().empty());
  EXPECT_TRUE(info.soname().empty());
  EXPECT_FALSE(info.compat_hash());
  EXPECT_FALSE(info.gnu_hash());
};

TEST(ElfldltlDynamicTests, SymbolInfoObserverEmpty) { TestAllFormats(SymbolInfoObserverEmptyTest); }

constexpr auto SymbolInfoObserverFullValidTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;
  using Sym = typename Elf::Sym;

  TestDiagnostics diag;
  SymbolInfoTestImage<Elf> test_image;
  elfldltl::DirectMemory image_memory = test_image.memory();

  // PT_DYNAMIC with full valid symbol info.
  const std::array dyn_goodsyms{
      Dyn{.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      Dyn{.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      Dyn{.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      Dyn{
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      Dyn{.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kGnuHash, .val = test_image.gnu_hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), image_memory, DynSpan(dyn_goodsyms),
                                      elfldltl::DynamicSymbolInfoObserver(info)),
              "%s", diag.ExplainErrors().c_str());

  EXPECT_EQ(0, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_TRUE(diag.errors().empty());

  EXPECT_EQ(info.strtab().size(), test_image.test_syms().strtab().size());
  EXPECT_EQ(info.strtab(), test_image.test_syms().strtab());
  EXPECT_EQ(info.safe_symtab().size(), test_image.test_syms().symtab().size());
  EXPECT_STR_EQ(info.soname(), "libfoo.so");
  EXPECT_TRUE(info.compat_hash());
  EXPECT_TRUE(info.gnu_hash());
};

TEST(ElfldltlDynamicTests, SymbolInfoObserverFullValid) {
  TestAllFormats(SymbolInfoObserverFullValidTest);
}

// We'll reuse that same image for the various error case tests.
// These cases only differ in their PT_DYNAMIC contents.

constexpr auto SymbolInfoObserverBadSonameOffsetTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;
  using Sym = typename Elf::Sym;

  TestDiagnostics diag;
  SymbolInfoTestImage<Elf> test_image;
  elfldltl::DirectMemory image_memory = test_image.memory();

  const std::array dyn_bad_soname_offset{
      Dyn{
          .tag = elfldltl::ElfDynTag::kSoname,
          // This is an invalid string table offset.
          .val = static_cast<size_type>(test_image.test_syms().strtab().size()),
      },
      Dyn{.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      Dyn{.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      Dyn{
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      Dyn{.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kGnuHash, .val = test_image.gnu_hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), image_memory, DynSpan(dyn_bad_soname_offset),
                                      elfldltl::DynamicSymbolInfoObserver(info)),
              "%s", diag.ExplainErrors().c_str());
  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());
};

TEST(ElfldltlDynamicTests, SymbolInfoObserverBadSonameOffset) {
  TestAllFormats(SymbolInfoObserverBadSonameOffsetTest);
}

constexpr auto SymbolInfoObserverBadSymentTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;

  TestDiagnostics diag;
  SymbolInfoTestImage<Elf> test_image;
  elfldltl::DirectMemory image_memory = test_image.memory();

  const std::array dyn_bad_syment{
      Dyn{.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      Dyn{.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kSymEnt, .val = 17},  // Wrong size.
      Dyn{.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      Dyn{
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      Dyn{.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kGnuHash, .val = test_image.gnu_hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), image_memory, DynSpan(dyn_bad_syment),
                                      elfldltl::DynamicSymbolInfoObserver(info)),
              "%s", diag.ExplainErrors().c_str());
  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());
};

TEST(ElfldltlDynamicTests, SymbolInfoObserverBadSyment) {
  TestAllFormats(SymbolInfoObserverBadSymentTest);
}

constexpr auto SymbolInfoObserverMissingStrszTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Dyn = typename Elf::Dyn;
  using Sym = typename Elf::Sym;

  TestDiagnostics diag;
  SymbolInfoTestImage<Elf> test_image;
  elfldltl::DirectMemory image_memory = test_image.memory();

  const std::array dyn_missing_strsz{
      Dyn{.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      Dyn{.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      // DT_STRSZ omitted with DT_STRTAB present.
      Dyn{.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kGnuHash, .val = test_image.gnu_hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), image_memory, DynSpan(dyn_missing_strsz),
                                      elfldltl::DynamicSymbolInfoObserver(info)),
              "%s", diag.ExplainErrors().c_str());
  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());
};

TEST(ElfldltlDynamicTests, SymbolInfoObserverMissingStrsz) {
  TestAllFormats(SymbolInfoObserverMissingStrszTest);
}

constexpr auto SymbolInfoObserverMissingStrtabTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;
  using Sym = typename Elf::Sym;

  TestDiagnostics diag;
  SymbolInfoTestImage<Elf> test_image;
  elfldltl::DirectMemory image_memory = test_image.memory();

  const std::array dyn_missing_strtab{
      Dyn{.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      // DT_STRTAB omitted with DT_STRSZ present.
      Dyn{
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      Dyn{.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      Dyn{.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kGnuHash, .val = test_image.gnu_hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), image_memory, DynSpan(dyn_missing_strtab),
                                      elfldltl::DynamicSymbolInfoObserver(info)),
              "%s", diag.ExplainErrors().c_str());
  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());
};

TEST(ElfldltlDynamicTests, SymbolInfoObserverMissingStrtab) {
  TestAllFormats(SymbolInfoObserverMissingStrtabTest);
}

constexpr auto SymbolInfoObserverBadStrtabAddrTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;
  using Sym = typename Elf::Sym;

  TestDiagnostics diag;
  SymbolInfoTestImage<Elf> test_image;
  elfldltl::DirectMemory image_memory = test_image.memory();

  const std::array dyn_bad_strtab_addr{
      Dyn{.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      // This is an invalid address, before the image start.
      Dyn{.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.symtab_addr() - 1},
      Dyn{
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      Dyn{.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kGnuHash, .val = test_image.gnu_hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), image_memory, DynSpan(dyn_bad_strtab_addr),
                                      elfldltl::DynamicSymbolInfoObserver(info)),
              "%s", diag.ExplainErrors().c_str());
  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());
};

TEST(ElfldltlDynamicTests, SymbolInfoObserverBadStrtabAddr) {
  TestAllFormats(SymbolInfoObserverBadStrtabAddrTest);
}

constexpr auto SymbolInfoObserverBadSymtabAddrTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;
  using Sym = typename Elf::Sym;

  TestDiagnostics diag;
  SymbolInfoTestImage<Elf> test_image;
  elfldltl::DirectMemory image_memory = test_image.memory();

  // Since the symtab has no known bounds, bad addresses are only diagnosed via
  // the memory object and cause hard failure, not via the diag object where
  // keep_going causes success return.
  const std::array dyn_bad_symtab_addr{
      Dyn{.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      Dyn{
          .tag = elfldltl::ElfDynTag::kSymTab,
          // This is an invalid address, past the image end.
          .val = static_cast<size_type>(test_image.symtab_addr() + test_image.size_bytes()),
      },
      Dyn{.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      Dyn{.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      Dyn{
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      Dyn{.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kGnuHash, .val = test_image.gnu_hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_FALSE(elfldltl::DecodeDynamic(diag.diag(), image_memory, DynSpan(dyn_bad_symtab_addr),
                                       elfldltl::DynamicSymbolInfoObserver(info)),
               "%s", diag.ExplainErrors().c_str());
  EXPECT_EQ(0, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(0, diag.errors().size(), "%s", diag.ExplainErrors().c_str());
};

TEST(ElfldltlDynamicTests, SymbolInfoObserverBadSymtabAddr) {
  TestAllFormats(SymbolInfoObserverBadSymtabAddrTest);
}

constexpr auto SymbolInfoObserverBadSymtabAlignTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;
  using Sym = typename Elf::Sym;

  TestDiagnostics diag;
  SymbolInfoTestImage<Elf> test_image;
  elfldltl::DirectMemory image_memory = test_image.memory();

  // A misaligned symtab becomes a hard failure after diagnosis because it's
  // treated like a memory failure in addition to the diagnosed error.
  const std::array dyn_bad_symtab_align{
      Dyn{.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      Dyn{
          .tag = elfldltl::ElfDynTag::kSymTab,
          // This is misaligned vs alignof(Sym).
          .val = test_image.symtab_addr() + 2,
      },
      Dyn{.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      Dyn{.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      Dyn{
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      Dyn{.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kGnuHash, .val = test_image.gnu_hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_FALSE(elfldltl::DecodeDynamic(diag.diag(), image_memory, DynSpan(dyn_bad_symtab_align),
                                       elfldltl::DynamicSymbolInfoObserver(info)),
               "%s", diag.ExplainErrors().c_str());
  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());
};

TEST(ElfldltlDynamicTests, SymbolInfoObserverBadSymtabAlign) {
  TestAllFormats(SymbolInfoObserverBadSymtabAlignTest);
}

constexpr auto SymbolInfoObserverBadHashAddrTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;
  using Sym = typename Elf::Sym;

  TestDiagnostics diag;
  SymbolInfoTestImage<Elf> test_image;
  elfldltl::DirectMemory image_memory = test_image.memory();

  // Since DT_HASH has no known bounds, bad addresses are only diagnosed via
  // the memory object and cause hard failure, not via the diag object where
  // keep_going causes success return.
  const std::array dyn_bad_hash_addr{
      Dyn{.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      Dyn{.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      Dyn{.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      Dyn{
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      Dyn{
          .tag = elfldltl::ElfDynTag::kHash,
          // This is an invalid address, past the image end.
          .val = static_cast<size_type>(test_image.symtab_addr() + test_image.size_bytes()),
      },
      Dyn{.tag = elfldltl::ElfDynTag::kGnuHash, .val = test_image.gnu_hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_FALSE(elfldltl::DecodeDynamic(diag.diag(), image_memory, DynSpan(dyn_bad_hash_addr),
                                       elfldltl::DynamicSymbolInfoObserver(info)),
               "%s", diag.ExplainErrors().c_str());
  EXPECT_EQ(0, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(0, diag.errors().size(), "%s", diag.ExplainErrors().c_str());
};

TEST(ElfldltlDynamicTests, SymbolInfoObserverBadHashAddr) {
  TestAllFormats(SymbolInfoObserverBadHashAddrTest);
}

constexpr auto SymbolInfoObserverBadHashAlignTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;
  using Sym = typename Elf::Sym;

  TestDiagnostics diag;
  SymbolInfoTestImage<Elf> test_image;
  elfldltl::DirectMemory image_memory = test_image.memory();

  const std::array dyn_bad_hash_align{
      Dyn{.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      Dyn{.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      Dyn{.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      Dyn{
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      Dyn{
          .tag = elfldltl::ElfDynTag::kHash,
          // This is misaligned vs alignof(Word).
          .val = test_image.hash_addr() + 2,
      },
      Dyn{.tag = elfldltl::ElfDynTag::kGnuHash, .val = test_image.gnu_hash_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), image_memory, DynSpan(dyn_bad_hash_align),
                                      elfldltl::DynamicSymbolInfoObserver(info)),
              "%s", diag.ExplainErrors().c_str());
  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());
};

TEST(ElfldltlDynamicTests, SymbolInfoObserverBadHashAlign) {
  TestAllFormats(SymbolInfoObserverBadHashAlignTest);
}

constexpr auto SymbolInfoObserverBadGnuHashAddrTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;
  using Sym = typename Elf::Sym;

  TestDiagnostics diag;
  SymbolInfoTestImage<Elf> test_image;
  elfldltl::DirectMemory image_memory = test_image.memory();

  // Since DT_GNU_HASH has no known bounds, bad addresses are only diagnosed
  // via the memory object and cause hard failure, not via the diag object
  // where keep_going causes success return.
  const std::array dyn_bad_gnu_hash_addr{
      Dyn{.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      Dyn{.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      Dyn{.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      Dyn{
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      Dyn{.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      Dyn{
          .tag = elfldltl::ElfDynTag::kGnuHash,
          // This is an invalid address, past the image end.
          .val = static_cast<size_type>(test_image.symtab_addr() + test_image.size_bytes()),
      },
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_FALSE(elfldltl::DecodeDynamic(diag.diag(), image_memory, DynSpan(dyn_bad_gnu_hash_addr),
                                       elfldltl::DynamicSymbolInfoObserver(info)),
               "%s", diag.ExplainErrors().c_str());
  EXPECT_EQ(0, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(0, diag.errors().size(), "%s", diag.ExplainErrors().c_str());
};

TEST(ElfldltlDynamicTests, SymbolInfoObserverBadGnuHashAddr) {
  TestAllFormats(SymbolInfoObserverBadGnuHashAddrTest);
}

constexpr auto SymbolInfoObserverBadGnuHashAlignTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;
  using Sym = typename Elf::Sym;

  TestDiagnostics diag;
  SymbolInfoTestImage<Elf> test_image;
  elfldltl::DirectMemory image_memory = test_image.memory();

  const std::array dyn_bad_gnu_hash_align{
      Dyn{.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      Dyn{.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      Dyn{.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      Dyn{.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      Dyn{
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      Dyn{.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      Dyn{
          .tag = elfldltl::ElfDynTag::kGnuHash,
          // This is misaligned vs alignof(size_type).
          .val = test_image.hash_addr() + sizeof(size_type) - 1,
      },
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), image_memory, DynSpan(dyn_bad_gnu_hash_align),
                                      elfldltl::DynamicSymbolInfoObserver(info)),
              "%s", diag.ExplainErrors().c_str());
  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());
};

TEST(ElfldltlDynamicTests, SymbolInfoObserverBadGnuHashAlign) {
  TestAllFormats(SymbolInfoObserverBadGnuHashAlignTest);
}

}  // namespace
