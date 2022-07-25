// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/layout.h>
#include <lib/elfldltl/machine.h>

#include <string_view>
#include <tuple>
#include <type_traits>

#include "tests.h"

namespace {

using Elf32Little = elfldltl::Elf32<elfldltl::ElfData::k2Lsb>;
using Elf64Little = elfldltl::Elf64<elfldltl::ElfData::k2Lsb>;
using Elf32Big = elfldltl::Elf32<elfldltl::ElfData::k2Msb>;
using Elf64Big = elfldltl::Elf64<elfldltl::ElfData::k2Msb>;

TEST(ElfldltlLayoutTests, Magic) {
  constexpr std::array<uint8_t, 4> kMagic = {0x7f, 'E', 'L', 'F'};
  EXPECT_BYTES_EQ(kMagic.data(), &Elf32Little::Ehdr::kMagic, 4);
  EXPECT_BYTES_EQ(kMagic.data(), &Elf64Little::Ehdr::kMagic, 4);
  EXPECT_BYTES_EQ(kMagic.data(), &Elf32Big::Ehdr::kMagic, 4);
  EXPECT_BYTES_EQ(kMagic.data(), &Elf64Big::Ehdr::kMagic, 4);
}

TEST(ElfldltlLayoutTests, Sizes) {
  static_assert(sizeof(elfldltl::Elf32<>::Ehdr) == 52);
  static_assert(alignof(elfldltl::Elf32<>::Ehdr) <= 4);
  static_assert(std::has_unique_object_representations_v<elfldltl::Elf32<>::Ehdr>);

  static_assert(sizeof(elfldltl::Elf64<>::Ehdr) == 64);
  static_assert(alignof(elfldltl::Elf64<>::Ehdr) <= 8);
  static_assert(std::has_unique_object_representations_v<elfldltl::Elf64<>::Ehdr>);

  static_assert(sizeof(elfldltl::Elf32<>::Phdr) == 32);
  static_assert(alignof(elfldltl::Elf32<>::Phdr) <= 4);
  static_assert(std::has_unique_object_representations_v<elfldltl::Elf32<>::Phdr>);

  static_assert(sizeof(elfldltl::Elf64<>::Phdr) == 56);
  static_assert(alignof(elfldltl::Elf64<>::Phdr) <= 8);
  static_assert(std::has_unique_object_representations_v<elfldltl::Elf64<>::Phdr>);

  static_assert(sizeof(elfldltl::Elf32<>::Shdr) == 40);
  static_assert(alignof(elfldltl::Elf32<>::Shdr) <= 4);
  static_assert(std::has_unique_object_representations_v<elfldltl::Elf32<>::Shdr>);

  static_assert(sizeof(elfldltl::Elf64<>::Shdr) == 64);
  static_assert(alignof(elfldltl::Elf64<>::Shdr) <= 8);
  static_assert(std::has_unique_object_representations_v<elfldltl::Elf64<>::Shdr>);

  static_assert(sizeof(elfldltl::Elf32<>::Dyn) == 8);
  static_assert(alignof(elfldltl::Elf32<>::Dyn) <= 4);
  static_assert(std::has_unique_object_representations_v<elfldltl::Elf32<>::Dyn>);

  static_assert(sizeof(elfldltl::Elf64<>::Dyn) == 16);
  static_assert(alignof(elfldltl::Elf64<>::Dyn) <= 8);
  static_assert(std::has_unique_object_representations_v<elfldltl::Elf64<>::Dyn>);

  static_assert(sizeof(elfldltl::Elf32<>::Sym) == 16);
  static_assert(alignof(elfldltl::Elf32<>::Sym) <= 4);
  static_assert(std::has_unique_object_representations_v<elfldltl::Elf32<>::Sym>);

  static_assert(sizeof(elfldltl::Elf64<>::Sym) == 24);
  static_assert(alignof(elfldltl::Elf64<>::Sym) <= 8);
  static_assert(std::has_unique_object_representations_v<elfldltl::Elf64<>::Sym>);

  static_assert(sizeof(elfldltl::Elf32<>::Rel) == 8);
  static_assert(alignof(elfldltl::Elf32<>::Rel) <= 4);
  static_assert(std::has_unique_object_representations_v<elfldltl::Elf32<>::Rel>);

  static_assert(sizeof(elfldltl::Elf64<>::Rel) == 16);
  static_assert(alignof(elfldltl::Elf64<>::Rel) <= 8);
  static_assert(std::has_unique_object_representations_v<elfldltl::Elf64<>::Rel>);

  static_assert(sizeof(elfldltl::Elf32<>::Rela) == 12);
  static_assert(alignof(elfldltl::Elf32<>::Rela) <= 4);
  static_assert(std::has_unique_object_representations_v<elfldltl::Elf32<>::Rela>);

  static_assert(sizeof(elfldltl::Elf64<>::Rela) == 24);
  static_assert(alignof(elfldltl::Elf64<>::Rela) <= 8);
  static_assert(std::has_unique_object_representations_v<elfldltl::Elf64<>::Rela>);
}

// Just instantiating this tests the constexpr Elf::Ehdr methods.
template <class Elf, auto Machine>
struct EhdrTests {
  static constexpr typename Elf::Ehdr kGood = {
      .magic = Elf::Ehdr::kMagic,
      .elfclass = Elf::kClass,
      .elfdata = Elf::kData,
      .ident_version = elfldltl::ElfVersion::kCurrent,
      .machine = Machine,
      .version = elfldltl::ElfVersion::kCurrent,
      .ehsize = sizeof(kGood),
  };
  static_assert(kGood.Valid());

  static constexpr typename Elf::Ehdr kBadMagic = {
      .magic = 0xabcdef,
      .elfclass = Elf::kClass,
      .elfdata = Elf::kData,
      .ident_version = elfldltl::ElfVersion::kCurrent,
      .machine = Machine,
      .version = elfldltl::ElfVersion::kCurrent,
      .ehsize = sizeof(kBadMagic),
  };
  static_assert(!kBadMagic.Valid());
  static void BadMagicDiagnostic() {
    ExpectedSingleError expected{"not an ELF file"};
    EXPECT_FALSE(kBadMagic.Valid(expected.diag()));
  }

  static constexpr typename Elf::Ehdr kBadIdentVersion = {
      .magic = Elf::Ehdr::kMagic,
      .elfclass = Elf::kClass,
      .elfdata = Elf::kData,
      .machine = Machine,
      .version = elfldltl::ElfVersion::kCurrent,
      .ehsize = sizeof(kBadIdentVersion),
  };
  static_assert(!kBadIdentVersion.Valid());
  static void BadIdentVersionDiagnostic() {
    ExpectedSingleError expected{"wrong EI_VERSION value"};
    EXPECT_FALSE(kBadIdentVersion.Valid(expected.diag()));
  }

  static constexpr typename Elf::Ehdr kBadVersion = {
      .magic = Elf::Ehdr::kMagic,
      .elfclass = Elf::kClass,
      .elfdata = Elf::kData,
      .ident_version = elfldltl::ElfVersion::kCurrent,
      .machine = Machine,
      .ehsize = sizeof(kBadVersion),
  };
  static_assert(!kBadVersion.Valid());
  static void BadVersionDiagnostic() {
    ExpectedSingleError expected{"wrong e_version value"};
    EXPECT_FALSE(kBadVersion.Valid(expected.diag()));
  }

  static constexpr typename Elf::Ehdr kBadSize = {
      .magic = Elf::Ehdr::kMagic,
      .elfclass = Elf::kClass,
      .elfdata = Elf::kData,
      .ident_version = elfldltl::ElfVersion::kCurrent,
      .machine = Machine,
      .version = elfldltl::ElfVersion::kCurrent,
      .ehsize = 17,
  };
  static_assert(!kBadSize.Valid());
  static void BadSizeDiagnostic() {
    ExpectedSingleError expected{"wrong e_ehsize value"};
    EXPECT_FALSE(kBadSize.Valid(expected.diag()));
  }

  static constexpr typename Elf::Ehdr kBadClass = {
      .magic = Elf::Ehdr::kMagic,
      .elfdata = Elf::kData,
      .ident_version = elfldltl::ElfVersion::kCurrent,
      .machine = Machine,
      .version = elfldltl::ElfVersion::kCurrent,
      .ehsize = sizeof(kBadClass),
  };
  static_assert(!kBadClass.Valid());
  static void BadClassDiagnostic() {
    ExpectedSingleError expected{"wrong ELF class (bit-width)"};
    EXPECT_FALSE(kBadClass.Valid(expected.diag()));
  }

  static constexpr auto kNotMyClass = Elf::kClass == elfldltl::ElfClass::k64  //
                                          ? elfldltl::ElfClass::k32
                                          : elfldltl::ElfClass::k64;
  static constexpr typename Elf::Ehdr kWrongClass = {
      .magic = Elf::Ehdr::kMagic,
      .elfclass = kNotMyClass,
      .elfdata = Elf::kData,
      .ident_version = elfldltl::ElfVersion::kCurrent,
      .machine = Machine,
      .version = elfldltl::ElfVersion::kCurrent,
      .ehsize = sizeof(kWrongClass),
  };
  static_assert(!kWrongClass.Valid());
  static void WrongClassDiagnostic() {
    ExpectedSingleError expected{"wrong ELF class (bit-width)"};
    EXPECT_FALSE(kWrongClass.Valid(expected.diag()));
  }

  static constexpr typename Elf::Ehdr kBadData = {
      .magic = Elf::Ehdr::kMagic,
      .elfclass = Elf::kClass,
      .ident_version = elfldltl::ElfVersion::kCurrent,
      .machine = Machine,
      .version = elfldltl::ElfVersion::kCurrent,
      .ehsize = sizeof(kBadData),
  };
  static_assert(!kBadData.Valid());
  static void BadDataDiagnostic() {
    ExpectedSingleError expected{"wrong byte order"};
    EXPECT_FALSE(kBadData.Valid(expected.diag()));
  }

  static constexpr auto kNotMyData = Elf::kData == elfldltl::ElfData::k2Lsb  //
                                         ? elfldltl::ElfData::k2Msb
                                         : elfldltl::ElfData::k2Lsb;
  static constexpr typename Elf::Ehdr kWrongData = {
      .magic = Elf::Ehdr::kMagic,
      .elfclass = Elf::kClass,
      .elfdata = kNotMyData,
      .ident_version = elfldltl::ElfVersion::kCurrent,
      .machine = Machine,
      .version = elfldltl::ElfVersion::kCurrent,
      .ehsize = sizeof(kWrongData),
  };
  static_assert(!kWrongData.Valid());
  static void WrongDataDiagnostic() {
    ExpectedSingleError expected{"wrong byte order"};
    EXPECT_FALSE(kWrongData.Valid(expected.diag()));
  }

  static constexpr typename Elf::Ehdr kExec = {
      .magic = Elf::Ehdr::kMagic,
      .elfclass = Elf::kClass,
      .elfdata = Elf::kData,
      .ident_version = elfldltl::ElfVersion::kCurrent,
      .type = elfldltl::ElfType::kExec,
      .machine = Machine,
      .version = elfldltl::ElfVersion::kCurrent,
      .ehsize = sizeof(kExec),
  };
  static_assert(kExec.Valid());
  static_assert(!kExec.Loadable(Machine));
  static void ExecDiagnostic() {
    ExpectedSingleError expected_valid;
    EXPECT_TRUE(kExec.Valid(expected_valid.diag()));
    ExpectedSingleError expected_loadable{
        "loading ET_EXEC files is not supported, only ET_DYN files;"
        " be sure to compile and link as PIE (-fPIE, -pie)"};
    EXPECT_FALSE(kExec.Loadable(expected_loadable.diag(), Machine));
  }

  static constexpr typename Elf::Ehdr kDyn = {
      .magic = Elf::Ehdr::kMagic,
      .elfclass = Elf::kClass,
      .elfdata = Elf::kData,
      .ident_version = elfldltl::ElfVersion::kCurrent,
      .type = elfldltl::ElfType::kDyn,
      .machine = Machine,
      .version = elfldltl::ElfVersion::kCurrent,
      .ehsize = sizeof(kDyn),
  };
  static_assert(kDyn.Valid());
  static_assert(kDyn.Loadable(Machine));
  static void DynDiagnostic() {
    ExpectedSingleError expected;
    EXPECT_TRUE(kDyn.Valid(expected.diag()));
    EXPECT_TRUE(kDyn.Loadable(expected.diag(), Machine));
  }

  static constexpr typename Elf::Ehdr kCore = {
      .magic = Elf::Ehdr::kMagic,
      .elfclass = Elf::kClass,
      .elfdata = Elf::kData,
      .ident_version = elfldltl::ElfVersion::kCurrent,
      .type = elfldltl::ElfType::kCore,
      .machine = Machine,
      .version = elfldltl::ElfVersion::kCurrent,
      .ehsize = sizeof(kCore),
  };
  static_assert(kCore.Valid());
  static_assert(!kCore.Loadable(Machine));
  static void CoreDiagnostic() {
    ExpectedSingleError expected_valid;
    EXPECT_TRUE(kCore.Valid(expected_valid.diag()));
    ExpectedSingleError expected_loadable{"ET_CORE files cannot be loaded"};
    EXPECT_FALSE(kCore.Loadable(expected_loadable.diag(), Machine));
  }

  static constexpr typename Elf::Ehdr kWrongMachine = {
      .magic = Elf::Ehdr::kMagic,
      .elfclass = Elf::kClass,
      .elfdata = Elf::kData,
      .ident_version = elfldltl::ElfVersion::kCurrent,
      .type = elfldltl::ElfType::kDyn,
      .machine = elfldltl::ElfMachine::kNone,
      .version = elfldltl::ElfVersion::kCurrent,
      .ehsize = sizeof(kDyn),
  };
  static_assert(kWrongMachine.Valid());
  static_assert(!kWrongMachine.Loadable(Machine));
  static void WrongMachineDiagnostic() {
    ExpectedSingleError expected_valid;
    EXPECT_TRUE(kWrongMachine.Valid(expected_valid.diag()));
    ExpectedSingleError expected_loadable{"wrong e_machine for architecture"};
    EXPECT_FALSE(kWrongMachine.Loadable(expected_loadable.diag(), Machine));
  }

  static void DiagnosticsTests() {
    ASSERT_NO_FATAL_FAILURE(BadMagicDiagnostic());
    ASSERT_NO_FATAL_FAILURE(BadIdentVersionDiagnostic());
    ASSERT_NO_FATAL_FAILURE(BadVersionDiagnostic());
    ASSERT_NO_FATAL_FAILURE(BadSizeDiagnostic());
    ASSERT_NO_FATAL_FAILURE(BadClassDiagnostic());
    ASSERT_NO_FATAL_FAILURE(WrongClassDiagnostic());
    ASSERT_NO_FATAL_FAILURE(BadDataDiagnostic());
    ASSERT_NO_FATAL_FAILURE(WrongDataDiagnostic());
    ASSERT_NO_FATAL_FAILURE(ExecDiagnostic());
    ASSERT_NO_FATAL_FAILURE(DynDiagnostic());
    ASSERT_NO_FATAL_FAILURE(CoreDiagnostic());
    ASSERT_NO_FATAL_FAILURE(WrongMachineDiagnostic());
  }
};

template <class Elf>
struct AllMachinesEhdrTests {
  template <elfldltl::ElfMachine... Machine>
  using Tests = std::tuple<EhdrTests<Elf, Machine>...>;

  static constexpr elfldltl::AllSupportedMachines<Tests> kTests;
};

template <class... Elf>
using AllFormatsEhdrTests = std::tuple<AllMachinesEhdrTests<Elf>...>;

// This instantiates all the types to do their static_assert checks.
[[maybe_unused]] elfldltl::AllFormats<AllFormatsEhdrTests> kEhdrTests;

constexpr auto DiagnosticsTests = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  std::apply([](auto... test) { (decltype(test)::DiagnosticsTests(), ...); },
             AllMachinesEhdrTests<Elf>::kTests);
};

TEST(ElfldltlLayoutTests, DiagnosticsTests) {
  ASSERT_NO_FATAL_FAILURE(TestAllFormats(DiagnosticsTests));
}

}  // namespace
