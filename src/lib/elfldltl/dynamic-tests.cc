// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/dynamic.h>
#include <lib/elfldltl/machine.h>
#include <lib/elfldltl/memory.h>

#include <string>
#include <type_traits>
#include <vector>

#include <zxtest/zxtest.h>

#include "symbol-tests.h"

namespace {

constexpr elfldltl::DiagnosticsFlags kDiagFlags = {.multiple_errors = true};

constexpr auto EmptyTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors);

  elfldltl::DirectMemory memory({}, 0);

  // Nothing but the terminator.
  constexpr typename Elf::Dyn dyn[] = {
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  // No matchers and nothing to match.
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag, memory, cpp20::span(dyn)));

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
  EXPECT_STREQ(errors.front(), "missing DT_NULL terminator in PT_DYNAMIC");
};

TEST(ElfldltlDynamicTests, MissingTerminator) { TestAllFormats(MissingTerminatorTest); }

constexpr auto RejectTextrelTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kDiagFlags);

  elfldltl::DirectMemory memory({}, 0);

  // PT_DYNAMIC without DT_TEXTREL.
  constexpr typename Elf::Dyn dyn_notextrel[] = {
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  EXPECT_TRUE(elfldltl::DecodeDynamic(diag, memory, cpp20::span(dyn_notextrel),
                                      elfldltl::DynamicTextrelRejectObserver{}));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  EXPECT_TRUE(errors.empty());

  // PT_DYNAMIC with DT_TEXTREL.
  constexpr typename Elf::Dyn dyn_textrel[] = {
      {.tag = elfldltl::ElfDynTag::kTextRel},
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  EXPECT_TRUE(elfldltl::DecodeDynamic(diag, memory, cpp20::span(dyn_textrel),
                                      elfldltl::DynamicTextrelRejectObserver{}));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_GE(errors.size(), 1);
  EXPECT_STREQ(errors.front(), elfldltl::DynamicTextrelRejectObserver::Message());
};

TEST(ElfldltlDynamicTests, RejectTextrel) { TestAllFormats(RejectTextrelTest); }

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

constexpr auto RelocationInfoObserverEmptyTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Dyn = typename Elf::Dyn;

  TestDiagnostics diag;
  elfldltl::DirectMemory empty_memory({}, 0);

  // PT_DYNAMIC with no reloc info.
  constexpr Dyn dyn_noreloc[] = {
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::RelocationInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), empty_memory, cpp20::span(dyn_noreloc),
                                      elfldltl::DynamicRelocationInfoObserver(info)),
              "%s", diag.ExplainErrors().c_str());

  EXPECT_EQ(0, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_TRUE(diag.errors().empty());

  EXPECT_TRUE(info.rel_relative().empty());
  EXPECT_TRUE(info.rel_symbolic().empty());
  EXPECT_TRUE(info.rela_relative().empty());
  EXPECT_TRUE(info.rela_symbolic().empty());
  EXPECT_TRUE(info.relr().empty());
  std::visit([](const auto& table) { EXPECT_TRUE(table.empty()); }, info.jmprel());
};

TEST(ElfldltlDynamicTests, RelocationInfoObserverEmpty) {
  TestAllFormats(RelocationInfoObserverEmptyTest);
}

// This synthesizes a memory image of relocation test data with known
// offsets and addresses that can be referenced in dynamic section entries in
// the specific test data.  The same image contents are used for several tests
// below with different dynamic section data.  Because the Memory API admits
// mutation of the image, the same image buffer shouldn't be reused for
// multiple tests just in case a test mutates the buffer (though they are meant
// not to).  So this helper object is created in each test case to reconstruct
// the same data afresh.
template <typename Elf>
class RelocInfoTestImage {
 public:
  using size_type = typename Elf::size_type;
  using Addr = typename Elf::Addr;
  using Dyn = typename Elf::Dyn;
  using Rel = typename Elf::Rel;
  using Rela = typename Elf::Rela;
  using Sym = typename Elf::Sym;

  static size_type size_bytes() { return sizeof(image_); }

  static size_type image_addr() { return kImageAddr; }

  static size_type rel_size_bytes() { return sizeof(image_.rel); }

  static size_type relent_size_bytes() { return sizeof(image_.rel[0]); }

  static size_type rela_size_bytes() { return sizeof(image_.rela); }

  static size_type relaent_size_bytes() { return sizeof(image_.rela[0]); }

  static size_type relr_size_bytes() { return sizeof(image_.relr); }

  static size_type relrent_size_bytes() { return sizeof(image_.relr[0]); }

  size_type rel_addr() const { return ImageAddr(image_.rel); }

  size_type rela_addr() const { return ImageAddr(image_.rela); }

  size_type relr_addr() const { return ImageAddr(image_.relr); }

  elfldltl::DirectMemory memory() { return elfldltl::DirectMemory(image_bytes(), kImageAddr); }

 private:
  // Build up some good relocation data in a memory image.

  static constexpr size_type kImageAddr = 0x123400;
  static constexpr auto kTestMachine = elfldltl::ElfMachine::kNone;
  using TestType = elfldltl::RelocationTraits<kTestMachine>::Type;
  static constexpr uint32_t kRelativeType = static_cast<uint32_t>(TestType::kRelative);
  static constexpr uint32_t kAbsoluteType = static_cast<uint32_t>(TestType::kAbsolute);

  template <typename T>
  size_type ImageAddr(const T& data) const {
    return static_cast<size_type>(reinterpret_cast<const std::byte*>(&data) -
                                  reinterpret_cast<const std::byte*>(&image_)) +
           kImageAddr;
  }

  struct ImageData {
    Rel rel[3] = {
        {8, kRelativeType},
        {24, kRelativeType},
        {4096, kAbsoluteType},
    };

    Rela rela[3] = {
        {{8, kRelativeType}, 0x11111111},
        {{24, kRelativeType}, 0x33333333},
        {{4096, kAbsoluteType}, 0x1234},
    };

    Addr relr[3] = {
        32,
        0x55555555,
        0xaaaaaaaa | 1,
    };
  } image_;

  cpp20::span<std::byte> image_bytes() { return cpp20::as_writable_bytes(cpp20::span(&image_, 1)); }
};

constexpr auto RelocationInfoObserverFullValidTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;

  TestDiagnostics diag;
  RelocInfoTestImage<Elf> test_image;

  // PT_DYNAMIC with full valid reloc info.

  const Dyn dyn_goodreloc[] = {
      {
          .tag = elfldltl::ElfDynTag::kRel,
          .val = static_cast<size_type>(test_image.rel_addr()),
      },
      {.tag = elfldltl::ElfDynTag::kRelSz, .val = test_image.rel_size_bytes()},
      {
          .tag = elfldltl::ElfDynTag::kRelEnt,
          .val = test_image.relent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelCount, .val = 2},
      {
          .tag = elfldltl::ElfDynTag::kRela,
          .val = static_cast<size_type>(test_image.rela_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaSz,
          .val = test_image.rela_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaEnt,
          .val = test_image.relaent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelaCount, .val = 2},
      {
          .tag = elfldltl::ElfDynTag::kJmpRel,
          .val = static_cast<size_type>(test_image.rel_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRelSz,
          .val = test_image.rel_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRel,
          .val = static_cast<size_type>(elfldltl::ElfDynTag::kRel),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelr,
          .val = static_cast<size_type>(test_image.relr_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrSz,
          .val = test_image.relr_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrEnt,
          .val = test_image.relrent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::RelocationInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), test_image.memory(), cpp20::span(dyn_goodreloc),
                                      elfldltl::DynamicRelocationInfoObserver(info)),
              "%s", diag.ExplainErrors().c_str());

  EXPECT_EQ(0, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_TRUE(diag.errors().empty(), "%s", diag.ExplainErrors().c_str());

  EXPECT_EQ(2, info.rel_relative().size());
  EXPECT_EQ(1, info.rel_symbolic().size());
  EXPECT_EQ(2, info.rela_relative().size());
  EXPECT_EQ(1, info.rela_symbolic().size());
  EXPECT_EQ(3, info.relr().size());
  std::visit([](const auto& table) { EXPECT_EQ(3, table.size()); }, info.jmprel());
};

TEST(ElfldltlDynamicTests, RelocationInfoObserverFullValid) {
  TestAllFormats(RelocationInfoObserverFullValidTest);
}

// We'll reuse that same image for the various error case tests.
// These cases only differ in their PT_DYNAMIC contents.

constexpr auto RelocationInfoObserverBadRelentTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;

  TestDiagnostics diag;
  RelocInfoTestImage<Elf> test_image;

  const Dyn dyn_bad_relent[] = {
      {
          .tag = elfldltl::ElfDynTag::kRel,
          .val = static_cast<size_type>(test_image.rel_addr()),
      },
      {.tag = elfldltl::ElfDynTag::kRelSz, .val = test_image.rel_size_bytes()},
      {.tag = elfldltl::ElfDynTag::kRelEnt, .val = 17},  // Wrong size.
      {.tag = elfldltl::ElfDynTag::kRelCount, .val = 2},

      {
          .tag = elfldltl::ElfDynTag::kRela,
          .val = static_cast<size_type>(test_image.rela_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaSz,
          .val = test_image.rela_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaEnt,
          .val = test_image.relaent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelaCount, .val = 2},

      {
          .tag = elfldltl::ElfDynTag::kJmpRel,
          .val = static_cast<size_type>(test_image.rel_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRelSz,
          .val = test_image.rel_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRel,
          .val = static_cast<size_type>(elfldltl::ElfDynTag::kRel),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelr,
          .val = static_cast<size_type>(test_image.relr_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrSz,
          .val = test_image.relr_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrEnt,
          .val = test_image.relrent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::RelocationInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), test_image.memory(), cpp20::span(dyn_bad_relent),
                                      elfldltl::DynamicRelocationInfoObserver(info)),
              "%s", diag.ExplainErrors().c_str());

  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());

  // With keep-going, the data is delivered anyway.
  EXPECT_EQ(2, info.rel_relative().size());
  EXPECT_EQ(1, info.rel_symbolic().size());
  EXPECT_EQ(2, info.rela_relative().size());
  EXPECT_EQ(1, info.rela_symbolic().size());
  EXPECT_EQ(3, info.relr().size());
  std::visit([](const auto& table) { EXPECT_EQ(3, table.size()); }, info.jmprel());
};

TEST(ElfldltlDynamicTests, RelocationInfoObserverBadRelent) {
  TestAllFormats(RelocationInfoObserverBadRelentTest);
}

constexpr auto RelocationInfoObserverBadRelaentTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;

  TestDiagnostics diag;
  RelocInfoTestImage<Elf> test_image;

  const Dyn dyn_bad_relaent[] = {
      {
          .tag = elfldltl::ElfDynTag::kRel,
          .val = static_cast<size_type>(test_image.rel_addr()),
      },
      {.tag = elfldltl::ElfDynTag::kRelSz, .val = test_image.rel_size_bytes()},
      {
          .tag = elfldltl::ElfDynTag::kRelEnt,
          .val = test_image.relent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelCount, .val = 2},

      {
          .tag = elfldltl::ElfDynTag::kRela,
          .val = static_cast<size_type>(test_image.rela_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaSz,
          .val = test_image.rela_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelaEnt, .val = 17},  // Wrong size.
      {.tag = elfldltl::ElfDynTag::kRelaCount, .val = 2},

      {
          .tag = elfldltl::ElfDynTag::kJmpRel,
          .val = static_cast<size_type>(test_image.rel_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRelSz,
          .val = test_image.rel_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRel,
          .val = static_cast<size_type>(elfldltl::ElfDynTag::kRel),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelr,
          .val = static_cast<size_type>(test_image.relr_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrSz,
          .val = test_image.relr_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrEnt,
          .val = test_image.relrent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::RelocationInfo<Elf> info;
  EXPECT_TRUE(
      elfldltl::DecodeDynamic(diag.diag(), test_image.memory(), cpp20::span(dyn_bad_relaent),
                              elfldltl::DynamicRelocationInfoObserver(info)),
      "%s", diag.ExplainErrors().c_str());

  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());

  // With keep-going, the data is delivered anyway.
  EXPECT_EQ(2, info.rel_relative().size());
  EXPECT_EQ(1, info.rel_symbolic().size());
  EXPECT_EQ(2, info.rela_relative().size());
  EXPECT_EQ(1, info.rela_symbolic().size());
  EXPECT_EQ(3, info.relr().size());
  std::visit([](const auto& table) { EXPECT_EQ(3, table.size()); }, info.jmprel());
};

TEST(ElfldltlDynamicTests, RelocationInfoObserverBadRelaent) {
  TestAllFormats(RelocationInfoObserverBadRelaentTest);
}

constexpr auto RelocationInfoObserverBadRelrentTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;

  TestDiagnostics diag;
  RelocInfoTestImage<Elf> test_image;

  const Dyn dyn_bad_relrent[] = {
      {
          .tag = elfldltl::ElfDynTag::kRel,
          .val = static_cast<size_type>(test_image.rel_addr()),
      },
      {.tag = elfldltl::ElfDynTag::kRelSz, .val = test_image.rel_size_bytes()},
      {
          .tag = elfldltl::ElfDynTag::kRelEnt,
          .val = test_image.relent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelCount, .val = 2},

      {
          .tag = elfldltl::ElfDynTag::kRela,
          .val = static_cast<size_type>(test_image.rela_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaSz,
          .val = test_image.rela_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaEnt,
          .val = test_image.relaent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelaCount, .val = 2},

      {
          .tag = elfldltl::ElfDynTag::kJmpRel,
          .val = static_cast<size_type>(test_image.rel_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRelSz,
          .val = test_image.rel_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRel,
          .val = static_cast<size_type>(elfldltl::ElfDynTag::kRel),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelr,
          .val = static_cast<size_type>(test_image.relr_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrSz,
          .val = test_image.relr_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelrEnt, .val = 3},  // Wrong size.
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::RelocationInfo<Elf> info;
  EXPECT_TRUE(
      elfldltl::DecodeDynamic(diag.diag(), test_image.memory(), cpp20::span(dyn_bad_relrent),
                              elfldltl::DynamicRelocationInfoObserver(info)),
      "%s", diag.ExplainErrors().c_str());

  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());

  // With keep-going, the data is delivered anyway.
  EXPECT_EQ(2, info.rel_relative().size());
  EXPECT_EQ(1, info.rel_symbolic().size());
  EXPECT_EQ(2, info.rela_relative().size());
  EXPECT_EQ(1, info.rela_symbolic().size());
  EXPECT_EQ(3, info.relr().size());
  std::visit([](const auto& table) { EXPECT_EQ(3, table.size()); }, info.jmprel());
};

TEST(ElfldltlDynamicTests, RelocationInfoObserverBadRelrent) {
  TestAllFormats(RelocationInfoObserverBadRelrentTest);
}

constexpr auto RelocationInfoObserverMissingPltrelTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;

  TestDiagnostics diag;
  RelocInfoTestImage<Elf> test_image;

  const Dyn dyn_missing_pltrel[] = {
      {
          .tag = elfldltl::ElfDynTag::kRel,
          .val = static_cast<size_type>(test_image.rel_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelSz,
          .val = test_image.rel_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelEnt,
          .val = test_image.relent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelCount, .val = 2},
      {
          .tag = elfldltl::ElfDynTag::kRela,
          .val = static_cast<size_type>(test_image.rela_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaSz,
          .val = test_image.rela_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaEnt,
          .val = test_image.relaent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelaCount, .val = 2},
      {
          .tag = elfldltl::ElfDynTag::kJmpRel,
          .val = static_cast<size_type>(test_image.rel_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRelSz,
          .val = test_image.rel_size_bytes(),
      },
      // Missing DT_PLTREL.
      {
          .tag = elfldltl::ElfDynTag::kRelr,
          .val = static_cast<size_type>(test_image.relr_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrSz,
          .val = test_image.relr_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrEnt,
          .val = test_image.relrent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::RelocationInfo<Elf> info;
  EXPECT_TRUE(
      elfldltl::DecodeDynamic(diag.diag(), test_image.memory(), cpp20::span(dyn_missing_pltrel),
                              elfldltl::DynamicRelocationInfoObserver(info)),
      "%s", diag.ExplainErrors().c_str());

  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());

  // DT_JMPREL was ignored but the rest is normal.
  EXPECT_EQ(2, info.rel_relative().size());
  EXPECT_EQ(1, info.rel_symbolic().size());
  EXPECT_EQ(2, info.rela_relative().size());
  EXPECT_EQ(1, info.rela_symbolic().size());
  EXPECT_EQ(3, info.relr().size());
  std::visit([](const auto& table) { EXPECT_EQ(0, table.size()); }, info.jmprel());
};

TEST(ElfldltlDynamicTests, RelocationInfoObserverMissingPltrel) {
  TestAllFormats(RelocationInfoObserverMissingPltrelTest);
}

constexpr auto RelocationInfoObserverBadPltrelTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;

  TestDiagnostics diag;
  RelocInfoTestImage<Elf> test_image;

  const Dyn dyn_bad_pltrel[] = {
      {
          .tag = elfldltl::ElfDynTag::kRel,
          .val = static_cast<size_type>(test_image.rel_addr()),
      },
      {.tag = elfldltl::ElfDynTag::kRelSz, .val = test_image.rel_size_bytes()},
      {
          .tag = elfldltl::ElfDynTag::kRelEnt,
          .val = test_image.relent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelCount, .val = 2},
      {
          .tag = elfldltl::ElfDynTag::kRela,
          .val = static_cast<size_type>(test_image.rela_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaSz,
          .val = test_image.rela_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaEnt,
          .val = test_image.relaent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelaCount, .val = 2},
      {
          .tag = elfldltl::ElfDynTag::kJmpRel,
          .val = static_cast<size_type>(test_image.rel_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRelSz,
          .val = test_image.rel_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kPltRel, .val = 0},  // Invalid value.
      {
          .tag = elfldltl::ElfDynTag::kRelr,
          .val = static_cast<size_type>(test_image.relr_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrSz,
          .val = test_image.relr_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrEnt,
          .val = test_image.relrent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::RelocationInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), test_image.memory(), cpp20::span(dyn_bad_pltrel),
                                      elfldltl::DynamicRelocationInfoObserver(info)),
              "%s", diag.ExplainErrors().c_str());

  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());

  // DT_JMPREL was ignored but the rest is normal.
  EXPECT_EQ(2, info.rel_relative().size());
  EXPECT_EQ(1, info.rel_symbolic().size());
  EXPECT_EQ(2, info.rela_relative().size());
  EXPECT_EQ(1, info.rela_symbolic().size());
  EXPECT_EQ(3, info.relr().size());
  std::visit([](const auto& table) { EXPECT_EQ(0, table.size()); }, info.jmprel());
};

TEST(ElfldltlDynamicTests, RelocationInfoObserverBadPltrel) {
  TestAllFormats(RelocationInfoObserverBadPltrelTest);
}

// The bad address, size, and alignment cases are all the same template code
// paths for each table so we only test DT_REL to stand in for the rest.

constexpr auto RelocationInfoObserverBadRelAddrTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;

  TestDiagnostics diag;
  RelocInfoTestImage<Elf> test_image;

  const Dyn dyn_bad_rel_addr[] = {
      {
          .tag = elfldltl::ElfDynTag::kRel,
          // This is an invalid address, before the image starts.
          .val = test_image.image_addr() - 1,
      },
      {.tag = elfldltl::ElfDynTag::kRelSz, .val = test_image.rel_size_bytes()},
      {
          .tag = elfldltl::ElfDynTag::kRelEnt,
          .val = test_image.relent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelCount, .val = 2},
      {
          .tag = elfldltl::ElfDynTag::kRela,
          .val = static_cast<size_type>(test_image.rela_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaSz,
          .val = test_image.rela_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaEnt,
          .val = test_image.relaent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelaCount, .val = 2},
      {
          .tag = elfldltl::ElfDynTag::kJmpRel,
          .val = static_cast<size_type>(test_image.rel_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRelSz,
          .val = test_image.rel_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRel,
          .val = static_cast<size_type>(elfldltl::ElfDynTag::kRel),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelr,
          .val = static_cast<size_type>(test_image.relr_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrSz,
          .val = test_image.relr_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrEnt,
          .val = test_image.relrent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::RelocationInfo<Elf> info;
  EXPECT_TRUE(
      elfldltl::DecodeDynamic(diag.diag(), test_image.memory(), cpp20::span(dyn_bad_rel_addr),
                              elfldltl::DynamicRelocationInfoObserver(info)),
      "%s", diag.ExplainErrors().c_str());

  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());

  // DT_REL was ignored but the rest is normal.
  EXPECT_EQ(0, info.rel_relative().size());
  EXPECT_EQ(0, info.rel_symbolic().size());
  EXPECT_EQ(2, info.rela_relative().size());
  EXPECT_EQ(1, info.rela_symbolic().size());
  EXPECT_EQ(3, info.relr().size());
  std::visit([](const auto& table) { EXPECT_EQ(3, table.size()); }, info.jmprel());
};

TEST(ElfldltlDynamicTests, RelocationInfoObserverBadRelAddr) {
  TestAllFormats(RelocationInfoObserverBadRelAddrTest);
}

constexpr auto RelocationInfoObserverBadRelSzTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;

  TestDiagnostics diag;
  RelocInfoTestImage<Elf> test_image;

  const Dyn dyn_bad_relsz[] = {
      {.tag = elfldltl::ElfDynTag::kRel, .val = test_image.rel_addr()},
      {
          .tag = elfldltl::ElfDynTag::kRelSz,
          // This is an invalid size, bigger than the whole image.
          .val = test_image.size_bytes() + 1,
      },
      {
          .tag = elfldltl::ElfDynTag::kRelEnt,
          .val = test_image.relent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelCount, .val = 2},
      {
          .tag = elfldltl::ElfDynTag::kRela,
          .val = static_cast<size_type>(test_image.rela_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaSz,
          .val = test_image.rela_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaEnt,
          .val = test_image.relaent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelaCount, .val = 2},
      {
          .tag = elfldltl::ElfDynTag::kJmpRel,
          .val = static_cast<size_type>(test_image.rel_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRelSz,
          .val = test_image.rel_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRel,
          .val = static_cast<size_type>(elfldltl::ElfDynTag::kRel),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelr,
          .val = static_cast<size_type>(test_image.relr_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrSz,
          .val = test_image.relr_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrEnt,
          .val = test_image.relrent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::RelocationInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), test_image.memory(), cpp20::span(dyn_bad_relsz),
                                      elfldltl::DynamicRelocationInfoObserver(info)),
              "%s", diag.ExplainErrors().c_str());

  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());

  // DT_REL was ignored but the rest is normal.
  EXPECT_EQ(0, info.rel_relative().size());
  EXPECT_EQ(0, info.rel_symbolic().size());
  EXPECT_EQ(2, info.rela_relative().size());
  EXPECT_EQ(1, info.rela_symbolic().size());
  EXPECT_EQ(3, info.relr().size());
  std::visit([](const auto& table) { EXPECT_EQ(3, table.size()); }, info.jmprel());
};

TEST(ElfldltlDynamicTests, RelocationInfoObserverBadRelSz) {
  TestAllFormats(RelocationInfoObserverBadRelSzTest);
}

constexpr auto RelocationInfoObserverBadRelSzAlignTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Dyn = typename Elf::Dyn;

  TestDiagnostics diag;
  RelocInfoTestImage<Elf> test_image;

  const Dyn dyn_bad_relsz_align[] = {
      {.tag = elfldltl::ElfDynTag::kRel, .val = test_image.rel_addr()},
      {
          .tag = elfldltl::ElfDynTag::kRelSz,
          // This size is not a multiple of the entry size.
          .val = test_image.rel_size_bytes() - 3,
      },
      {
          .tag = elfldltl::ElfDynTag::kRelEnt,
          .val = test_image.relent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelCount, .val = 2},
      {
          .tag = elfldltl::ElfDynTag::kRela,
          .val = static_cast<size_type>(test_image.rela_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaSz,
          .val = test_image.rela_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelaEnt,
          .val = test_image.relaent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kRelaCount, .val = 2},
      {
          .tag = elfldltl::ElfDynTag::kJmpRel,
          .val = static_cast<size_type>(test_image.rel_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRelSz,
          .val = test_image.rel_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kPltRel,
          .val = static_cast<size_type>(elfldltl::ElfDynTag::kRel),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelr,
          .val = static_cast<size_type>(test_image.relr_addr()),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrSz,
          .val = test_image.relr_size_bytes(),
      },
      {
          .tag = elfldltl::ElfDynTag::kRelrEnt,
          .val = test_image.relrent_size_bytes(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::RelocationInfo<Elf> info;
  EXPECT_TRUE(
      elfldltl::DecodeDynamic(diag.diag(), test_image.memory(), cpp20::span(dyn_bad_relsz_align),
                              elfldltl::DynamicRelocationInfoObserver(info)),
      "%s", diag.ExplainErrors().c_str());

  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());

  // DT_REL was ignored but the rest is normal.
  EXPECT_EQ(0, info.rel_relative().size());
  EXPECT_EQ(0, info.rel_symbolic().size());
  EXPECT_EQ(2, info.rela_relative().size());
  EXPECT_EQ(1, info.rela_symbolic().size());
  EXPECT_EQ(3, info.relr().size());
  std::visit([](const auto& table) { EXPECT_EQ(3, table.size()); }, info.jmprel());
};

TEST(ElfldltlDynamicTests, RelocationInfoObserverBadRelSzAlign) {
  TestAllFormats(RelocationInfoObserverBadRelSzAlignTest);
}

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

constexpr auto SymbolInfoObserverEmptyTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Dyn = typename Elf::Dyn;

  TestDiagnostics diag;
  elfldltl::DirectMemory empty_memory({}, 0);

  // PT_DYNAMIC with no symbol info.
  constexpr Dyn dyn_nosyms[] = {
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), empty_memory, cpp20::span(dyn_nosyms),
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

  // PT_DYNAMIC with full valid symbol info.
  const Dyn dyn_goodsyms[] = {
      {.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      {.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      {.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      {.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      {
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      {.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      {
          .tag = elfldltl::ElfDynTag::kGnuHash,
          .val = test_image.gnu_hash_addr(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), test_image.memory(), cpp20::span(dyn_goodsyms),
                                      elfldltl::DynamicSymbolInfoObserver(info)),
              "%s", diag.ExplainErrors().c_str());

  EXPECT_EQ(0, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_TRUE(diag.errors().empty());

  EXPECT_EQ(info.strtab().size(), test_image.test_syms().strtab().size());
  EXPECT_EQ(info.strtab(), test_image.test_syms().strtab());
  EXPECT_EQ(info.safe_symtab().size(), test_image.test_syms().symtab().size());
  EXPECT_STREQ(info.soname(), "libfoo.so");
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

  const Dyn dyn_bad_soname_offset[] = {
      {
          .tag = elfldltl::ElfDynTag::kSoname,
          // This is an invalid string table offset.
          .val = static_cast<size_type>(test_image.test_syms().strtab().size()),
      },
      {.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      {.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      {.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      {
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      {.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      {.tag = elfldltl::ElfDynTag::kGnuHash, .val = test_image.gnu_hash_addr()},
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), image_memory, cpp20::span(dyn_bad_soname_offset),
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

  const Dyn dyn_bad_syment[] = {
      {.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      {.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      {.tag = elfldltl::ElfDynTag::kSymEnt, .val = 17},  // Wrong size.
      {.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      {
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      {.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      {
          .tag = elfldltl::ElfDynTag::kGnuHash,
          .val = test_image.gnu_hash_addr(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), image_memory, cpp20::span(dyn_bad_syment),
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

  const Dyn dyn_missing_strsz[] = {
      {.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      {.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      {.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      // DT_STRSZ omitted with DT_STRTAB present.
      {.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      {
          .tag = elfldltl::ElfDynTag::kGnuHash,
          .val = test_image.gnu_hash_addr(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), image_memory, cpp20::span(dyn_missing_strsz),
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

  const Dyn dyn_missing_strtab[] = {
      {.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      // DT_STRTAB omitted with DT_STRSZ present.
      {
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      {.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      {.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      {
          .tag = elfldltl::ElfDynTag::kGnuHash,
          .val = test_image.gnu_hash_addr(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), image_memory, cpp20::span(dyn_missing_strtab),
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

  const Dyn dyn_bad_strtab_addr[] = {
      {.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      {.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      // This is an invalid address, before the image start.
      {
          .tag = elfldltl::ElfDynTag::kStrTab,
          .val = test_image.symtab_addr() - 1,
      },
      {
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      {.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      {
          .tag = elfldltl::ElfDynTag::kGnuHash,
          .val = test_image.gnu_hash_addr(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), image_memory, cpp20::span(dyn_bad_strtab_addr),
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
  const Dyn dyn_bad_symtab_addr[] = {
      {.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      {
          .tag = elfldltl::ElfDynTag::kSymTab,
          // This is an invalid address, past the image end.
          .val = static_cast<size_type>(test_image.symtab_addr() + test_image.size_bytes()),
      },
      {.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      {.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      {
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      {.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      {
          .tag = elfldltl::ElfDynTag::kGnuHash,
          .val = test_image.gnu_hash_addr(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_FALSE(elfldltl::DecodeDynamic(diag.diag(), image_memory, cpp20::span(dyn_bad_symtab_addr),
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
  const Dyn dyn_bad_symtab_align[] = {
      {.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      {
          .tag = elfldltl::ElfDynTag::kSymTab,
          // This is misaligned vs alignof(Sym).
          .val = test_image.symtab_addr() + 2,
      },
      {.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      {.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      {
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      {.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      {
          .tag = elfldltl::ElfDynTag::kGnuHash,
          .val = test_image.gnu_hash_addr(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_FALSE(elfldltl::DecodeDynamic(diag.diag(), image_memory, cpp20::span(dyn_bad_symtab_align),
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
  const Dyn dyn_bad_hash_addr[] = {
      {.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      {.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      {.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      {.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      {
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      {
          .tag = elfldltl::ElfDynTag::kHash,
          // This is an invalid address, past the image end.
          .val = static_cast<size_type>(test_image.symtab_addr() + test_image.size_bytes()),
      },
      {
          .tag = elfldltl::ElfDynTag::kGnuHash,
          .val = test_image.gnu_hash_addr(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_FALSE(elfldltl::DecodeDynamic(diag.diag(), image_memory, cpp20::span(dyn_bad_hash_addr),
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

  const Dyn dyn_bad_hash_align[] = {
      {.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      {.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      {.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      {.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      {
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      {
          .tag = elfldltl::ElfDynTag::kHash,
          // This is misaligned vs alignof(Word).
          .val = test_image.hash_addr() + 2,
      },
      {
          .tag = elfldltl::ElfDynTag::kGnuHash,
          .val = test_image.gnu_hash_addr(),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(elfldltl::DecodeDynamic(diag.diag(), image_memory, cpp20::span(dyn_bad_hash_align),
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
  const Dyn dyn_bad_gnu_hash_addr[] = {
      {.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      {.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      {.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      {.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      {
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      {.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      {
          .tag = elfldltl::ElfDynTag::kGnuHash,
          // This is an invalid address, past the image end.
          .val = static_cast<size_type>(test_image.symtab_addr() + test_image.size_bytes()),
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_FALSE(
      elfldltl::DecodeDynamic(diag.diag(), image_memory, cpp20::span(dyn_bad_gnu_hash_addr),
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

  const Dyn dyn_bad_gnu_hash_align[] = {
      {.tag = elfldltl::ElfDynTag::kSoname, .val = test_image.soname_offset()},
      {.tag = elfldltl::ElfDynTag::kSymTab, .val = test_image.symtab_addr()},
      {.tag = elfldltl::ElfDynTag::kSymEnt, .val = sizeof(Sym)},
      {.tag = elfldltl::ElfDynTag::kStrTab, .val = test_image.strtab_addr()},
      {
          .tag = elfldltl::ElfDynTag::kStrSz,
          .val = static_cast<size_type>(test_image.strtab_size_bytes()),
      },
      {.tag = elfldltl::ElfDynTag::kHash, .val = test_image.hash_addr()},
      {
          .tag = elfldltl::ElfDynTag::kGnuHash,
          // This is misaligned vs alignof(size_type).
          .val = test_image.hash_addr() + sizeof(size_type) - 1,
      },
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::SymbolInfo<Elf> info;
  EXPECT_TRUE(
      elfldltl::DecodeDynamic(diag.diag(), image_memory, cpp20::span(dyn_bad_gnu_hash_align),
                              elfldltl::DynamicSymbolInfoObserver(info)),
      "%s", diag.ExplainErrors().c_str());
  EXPECT_EQ(1, diag.diag().errors());
  EXPECT_EQ(0, diag.diag().warnings());
  EXPECT_EQ(1, diag.errors().size(), "%s", diag.ExplainErrors().c_str());
};

TEST(ElfldltlDynamicTests, SymbolInfoObserverBadGnuHashAlign) {
  TestAllFormats(SymbolInfoObserverBadGnuHashAlignTest);
}

template <typename Elf>
struct NotCalledSymbolInfo {
  std::string_view string(typename Elf::size_type) const {
    ADD_FAILURE();
    return {};
  }
};

constexpr auto ObserveNeededEmptyTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;

  auto diag = ExpectOkDiagnostics();

  elfldltl::DirectMemory memory({}, 0);

  NotCalledSymbolInfo<Elf> si;

  constexpr typename Elf::Dyn dyn[] = {
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  EXPECT_TRUE(elfldltl::DecodeDynamic(diag, memory, cpp20::span(dyn),
                                      elfldltl::DynamicNeededObserver(si, [](std::string_view) {
                                        ADD_FAILURE();
                                        return false;
                                      })));
};

TEST(ElfldltlDynamicTests, ObserveNeededEmpty) { TestAllFormats(ObserveNeededEmptyTest); }

constexpr auto ObserveNeededTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;

  auto diag = ExpectOkDiagnostics();

  elfldltl::DirectMemory memory({}, 0);

  elfldltl::SymbolInfo<Elf> si;

  constexpr std::string_view kNeededStrings[] = {"zero.so", "one.so", "two.so", "3.so"};
  TestSymtab<Elf> symtab;

  const typename Elf::Dyn dyn[] = {
      {.tag = elfldltl::ElfDynTag::kNeeded, .val = symtab.AddString(kNeededStrings[0])},
      {.tag = elfldltl::ElfDynTag::kNeeded, .val = symtab.AddString(kNeededStrings[1])},
      {.tag = elfldltl::ElfDynTag::kNeeded, .val = symtab.AddString(kNeededStrings[2])},
      {.tag = elfldltl::ElfDynTag::kNeeded, .val = symtab.AddString(kNeededStrings[3])},
      {.tag = elfldltl::ElfDynTag::kNull},
  };

  symtab.SetInfo(si);

  size_type current_index = 0;
  auto expect_next = [&](std::string_view needed) {
    EXPECT_STREQ(kNeededStrings[current_index++], needed);
    return true;
  };

  EXPECT_TRUE(elfldltl::DecodeDynamic(diag, memory, cpp20::span(dyn),
                                      elfldltl::DynamicNeededObserver(si, expect_next)));
};

TEST(ElfldltlDynamicTests, ObserveNeeded) { TestAllFormats(ObserveNeededTest); }

}  // namespace
