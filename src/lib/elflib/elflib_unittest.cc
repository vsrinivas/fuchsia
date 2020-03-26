// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/elflib/elflib.h"

#include <limits.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <algorithm>
#include <iterator>

#include "gtest/gtest.h"

namespace elflib {
namespace {

constexpr uint64_t kAddrPoison = 0xdeadb33ff00db4b3;
constexpr uint64_t kSymbolPoison = 0xb0bab0ba;
constexpr uint64_t kNoteGnuBuildId = 3;
constexpr uint64_t kMeaninglessNoteType = 42;

// The test files will be copied over to this specific location at build time.
constexpr char kStrippedExampleFile[] = "stripped_example.elf";
constexpr char kUnstrippedExampleFileBase[] = "unstripped_example";
constexpr char kUnstrippedExampleFileStrippedBase[] = "unstripped_example_stripped";

inline std::string GetTestBinaryPath(const std::string& bin) { return "/pkg/data/" + bin; }

class TestData {
 public:
  TestData(bool with_symbols) {
    PushData(Elf64_Ehdr{
        .e_ident = {0, 0, 0, 0, ELFCLASS64, ELFDATA2LSB, EV_CURRENT},
        .e_version = EV_CURRENT,
        .e_shoff = sizeof(Elf64_Ehdr),
        .e_ehsize = sizeof(Elf64_Ehdr),
        .e_shentsize = sizeof(Elf64_Shdr),
        .e_phentsize = sizeof(Elf64_Phdr),
        .e_shnum = 6,
        .e_phnum = 2,
        .e_shstrndx = 0,
    });

    *DataAt<char>(0) = ElfMagic[0];
    *DataAt<char>(1) = ElfMagic[1];
    *DataAt<char>(2) = ElfMagic[2];
    *DataAt<char>(3) = ElfMagic[3];

    size_t shstrtab_hdr = PushData(Elf64_Shdr{
        .sh_name = 1,
        .sh_type = SHT_STRTAB,
        .sh_size = 34,
        .sh_addr = kAddrPoison,
    });
    size_t stuff_hdr = PushData(Elf64_Shdr{
        .sh_name = 11,
        .sh_type = SHT_LOUSER,
        .sh_size = 15,
        .sh_addr = kAddrPoison,
    });
    size_t strtab_hdr = PushData(Elf64_Shdr{
        .sh_name = 18,
        .sh_type = SHT_STRTAB,
        .sh_size = 16,
        .sh_addr = kAddrPoison,
    });
    size_t symtab_hdr = PushData(Elf64_Shdr{
        .sh_name = 26,
        .sh_type = SHT_SYMTAB,
        .sh_size = sizeof(Elf64_Sym),
        .sh_addr = kAddrPoison,
    });
    PushData(Elf64_Shdr{
        .sh_name = 34,
        .sh_type = SHT_NULL,
        .sh_size = 0,
        .sh_addr = kAddrPoison,
    });
    PushData(Elf64_Shdr{
        .sh_name = 40,
        .sh_type = SHT_NOBITS,
        .sh_size = 0,
        .sh_addr = kAddrPoison,
    });

    size_t phnote_hdr = PushData(Elf64_Phdr{
        .p_type = PT_NOTE,
        .p_vaddr = kAddrPoison,
    });
    DataAt<Elf64_Ehdr>(0)->e_phoff = phnote_hdr;

    if (with_symbols) {
      DataAt<Elf64_Shdr>(shstrtab_hdr)->sh_offset =
          PushData("\0.shstrtab\0.stuff\0.strtab\0.symtab\0.null\0.nobits\0", 48);
    }

    DataAt<Elf64_Shdr>(stuff_hdr)->sh_offset = PushData("This is a test.", 15);

    if (with_symbols) {
      DataAt<Elf64_Shdr>(strtab_hdr)->sh_offset = PushData("\0zx_frob_handle\0", 16);
    }

    DataAt<Elf64_Shdr>(symtab_hdr)->sh_offset = PushData(Elf64_Sym{
        .st_name = 1,
        .st_shndx = SHN_COMMON,
        .st_value = kSymbolPoison,
        .st_size = 0,
    });

    size_t buildid_nhdr =
        PushData(Elf64_Nhdr{.n_namesz = 4, .n_descsz = 32, .n_type = kNoteGnuBuildId});

    DataAt<Elf64_Phdr>(phnote_hdr)->p_offset = buildid_nhdr;

    PushData("GNU\0", 4);

    uint8_t desc_data[32] = {
        0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7,
        0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7,
    };

    PushData(desc_data, 32);

    PushData(Elf64_Nhdr{
        .n_namesz = 6,
        .n_descsz = 3,
        .n_type = kMeaninglessNoteType,
    });

    PushData("seven\0\0\0", 8);
    PushData("foo\0", 4);

    DataAt<Elf64_Phdr>(phnote_hdr)->p_filesz = Pos() - buildid_nhdr;
    DataAt<Elf64_Phdr>(phnote_hdr)->p_memsz = Pos() - buildid_nhdr;
  }

  template <typename T>
  T* DataAt(size_t offset) {
    return reinterpret_cast<T*>(content_.data() + offset);
  }

  template <typename T>
  size_t PushData(T data) {
    return PushData(reinterpret_cast<uint8_t*>(&data), sizeof(data));
  }

  size_t PushData(const char* bytes, size_t size) {
    return PushData(reinterpret_cast<const uint8_t*>(bytes), size);
  }

  size_t PushData(const uint8_t* bytes, size_t size) {
    size_t offset = Pos();
    std::copy(bytes, bytes + size, std::back_inserter(content_));
    return offset;
  }

  size_t Pos() { return content_.size(); }
  size_t Size() { return content_.size(); }
  const uint8_t* Data() { return content_.data(); }

  std::function<bool(uint64_t, std::vector<uint8_t>*)> GetFetcher() {
    return [this](uint64_t offset, std::vector<uint8_t>* out) {
      if (offset + out->size() > content_.size()) {
        return false;
      }

      std::copy(content_.begin() + offset, content_.begin() + offset + out->size(), out->begin());
      return true;
    };
  }

 private:
  std::vector<uint8_t> content_;
};

}  // namespace

TEST(ElfLib, Create) {
  TestData t(/*with_symbols=*/true);
  std::unique_ptr<ElfLib> got;

  EXPECT_NE(ElfLib::Create(t.Data(), t.Size()).get(), nullptr);
}

TEST(ElfLib, GetSection) {
  TestData t(/*with_symbols=*/true);
  std::unique_ptr<ElfLib> elf = ElfLib::Create(t.Data(), t.Size());

  ASSERT_NE(elf.get(), nullptr);

  auto data = elf->GetSectionData(".stuff");
  const uint8_t* expected_content = reinterpret_cast<const uint8_t*>("This is a test.");

  ASSERT_NE(data.ptr, nullptr);

  auto expect = std::vector<uint8_t>(expected_content, expected_content + 15);
  auto got = std::vector<uint8_t>(data.ptr, data.ptr + data.size);

  EXPECT_EQ(expect, got);
}

TEST(ElfLib, GetSymbolValue) {
  TestData t(/*with_symbols=*/true);
  std::unique_ptr<ElfLib> elf = ElfLib::Create(t.Data(), t.Size());

  ASSERT_NE(elf.get(), nullptr);

  auto data = elf->GetSymbol("zx_frob_handle");
  ASSERT_TRUE(data);
  EXPECT_EQ(kSymbolPoison, data->st_value);
}

TEST(ElfLib, GetSymbolValueFromDebug) {
  TestData t1(/*with_symbols=*/false);
  TestData t2(/*with_symbols=*/true);
  std::unique_ptr<ElfLib> elf = ElfLib::Create(t1.Data(), t1.Size());
  std::unique_ptr<ElfLib> debug = ElfLib::Create(t2.Data(), t2.Size());
  elf->SetDebugData(std::move(debug));

  ASSERT_NE(elf.get(), nullptr);

  auto data = elf->GetSymbol("zx_frob_handle");
  ASSERT_TRUE(data);
  EXPECT_EQ(kSymbolPoison, data->st_value);
}

TEST(ElfLib, GetAllSymbols) {
  TestData t(/*with_symbols=*/true);
  std::unique_ptr<ElfLib> elf = ElfLib::Create(t.Data(), t.Size());

  ASSERT_NE(elf.get(), nullptr);

  auto syms = elf->GetAllSymbols();
  ASSERT_TRUE(syms);
  EXPECT_EQ(1U, syms->size());

  Elf64_Sym sym = (*syms)["zx_frob_handle"];
  EXPECT_EQ(1U, sym.st_name);
  EXPECT_EQ(0U, sym.st_size);
  EXPECT_EQ(SHN_COMMON, sym.st_shndx);
  EXPECT_EQ(kSymbolPoison, sym.st_value);
}

TEST(ElfLib, GetNote) {
  TestData t(/*with_symbols=*/true);
  std::unique_ptr<ElfLib> elf = ElfLib::Create(t.Data(), t.Size());

  ASSERT_NE(elf.get(), nullptr);

  auto got = elf->GetNote("GNU", kNoteGnuBuildId);

  EXPECT_TRUE(got);
  auto data = *got;

  EXPECT_EQ(32U, data.size());

  for (size_t i = 0; i < 32; i++) {
    EXPECT_EQ(i % 8, data[i]);
  }

  EXPECT_EQ("0001020304050607000102030405060700010203040506070001020304050607",
            elf->GetGNUBuildID());
}

TEST(ElfLib, MissingSections) {
  TestData t(/*with_symbols=*/true);
  std::unique_ptr<ElfLib> elf = ElfLib::Create(t.Data(), t.Size());

  ASSERT_NE(elf.get(), nullptr);

  auto got = elf->GetSectionData(".null");
  EXPECT_EQ(got.ptr, nullptr);
  got = elf->GetSectionData(".nobits");
  EXPECT_EQ(got.ptr, nullptr);
}

TEST(ElfLib, GetIrregularNote) {
  TestData t(/*with_symbols=*/true);
  std::unique_ptr<ElfLib> elf = ElfLib::Create(t.Data(), t.Size());

  ASSERT_NE(elf.get(), nullptr);

  auto got = elf->GetNote("seven", kMeaninglessNoteType);

  EXPECT_TRUE(got);
  auto data = *got;

  EXPECT_EQ(3U, data.size());

  EXPECT_EQ("foo", std::string(data.data(), data.data() + 3));
}

TEST(ElfLib, GetSymbolsFromStripped) {
  std::unique_ptr<ElfLib> elf = ElfLib::Create(GetTestBinaryPath(kStrippedExampleFile));

  ASSERT_NE(elf.get(), nullptr);

  auto missing_syms = elf->GetAllSymbols();
  EXPECT_FALSE(missing_syms);

  auto syms = elf->GetAllDynamicSymbols();
  ASSERT_TRUE(syms);
  EXPECT_EQ(8U, syms->size());

  std::map<std::string, Elf64_Sym>::iterator it;

  it = syms->find("");
  EXPECT_NE(it, syms->end());
  it = syms->find("__bss_start");
  EXPECT_NE(it, syms->end());
  it = syms->find("__libc_start_main");
  EXPECT_NE(it, syms->end());
  it = syms->find("__scudo_default_options");
  EXPECT_NE(it, syms->end());
  it = syms->find("_edata");
  EXPECT_NE(it, syms->end());
  it = syms->find("_end");
  EXPECT_NE(it, syms->end());
  it = syms->find("printf");
  EXPECT_NE(it, syms->end());
  it = syms->find("strlen");
  EXPECT_NE(it, syms->end());
}

TEST(ElfLib, GetPLTFromUnstripped) {
  std::string suffixes[] = {".elf", ".arm64.elf"};
  for (auto suffix : suffixes) {
    std::unique_ptr<ElfLib> elf =
        ElfLib::Create(GetTestBinaryPath(std::string(kUnstrippedExampleFileBase) + suffix));

    ASSERT_NE(elf.get(), nullptr);

    auto plt = elf->GetPLTOffsets();

    EXPECT_EQ(2U, plt.size());

    if (suffix == ".elf") {
      // x86
      EXPECT_EQ(0x15d0U, plt["printf"]);
      EXPECT_EQ(0x15e0U, plt["strlen"]);
    } else {
      // arm
      EXPECT_EQ(0x107B0U, plt["printf"]);
      EXPECT_EQ(0x107C0U, plt["strlen"]);
    }
  }
}

TEST(ElfLib, GetPLTFromStrippedDebug) {
  std::string suffixes[] = {".elf", ".arm64.elf"};
  for (auto& suffix : suffixes) {
    std::unique_ptr<ElfLib> elf =
        ElfLib::Create(GetTestBinaryPath(std::string(kUnstrippedExampleFileStrippedBase) + suffix));
    std::unique_ptr<ElfLib> debug =
        ElfLib::Create(GetTestBinaryPath(std::string(kUnstrippedExampleFileBase) + suffix));

    ASSERT_NE(elf.get(), nullptr);
    ASSERT_NE(debug.get(), nullptr);

    ASSERT_TRUE(elf->SetDebugData(std::move(debug)));

    auto plt = elf->GetPLTOffsets();

    EXPECT_EQ(2U, plt.size());

    if (suffix == ".elf") {
      // x86
      EXPECT_EQ(0x15d0U, plt["printf"]);
      EXPECT_EQ(0x15e0U, plt["strlen"]);
    } else {
      // arm
      EXPECT_EQ(0x107B0U, plt["printf"]);
      EXPECT_EQ(0x107C0U, plt["strlen"]);
    }
  }
}

TEST(ElfLib, DetectUnstripped) {
  std::unique_ptr<ElfLib> elf =
      ElfLib::Create(GetTestBinaryPath(std::string(kUnstrippedExampleFileBase) + ".elf"));

  ASSERT_NE(elf.get(), nullptr);

  EXPECT_TRUE(elf->ProbeHasDebugInfo());
  EXPECT_TRUE(elf->ProbeHasProgramBits());
}

TEST(ElfLib, DetectStripped) {
  std::unique_ptr<ElfLib> elf =
      ElfLib::Create(GetTestBinaryPath(std::string(kUnstrippedExampleFileStrippedBase) + ".elf"));

  ASSERT_NE(elf.get(), nullptr);

  EXPECT_FALSE(elf->ProbeHasDebugInfo());
  EXPECT_TRUE(elf->ProbeHasProgramBits());
}

TEST(ElfLib, SectionOverflow) {
  // This test case was found by clusterfuzz. This is the reproducer it found. The function of the
  // test is to have a section with a size and offset that, when added together, result in an
  // overflow. This can break bounds checking and hopefully trick us into an out of bounds read.
  const uint8_t kData[] = {
      0x7f, 0x45, 0x4c, 0x46, 0x02, 0xe2, 0x01, 0xff, 0x05, 0xff, 0xff, 0x5b, 0xff, 0x00,
      0x9a, 0x00, 0x00, 0x00, 0x45, 0x5b, 0x01, 0x00, 0x00, 0x00, 0xf6, 0x05, 0x9f, 0x9f,
      0x9f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x9f, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00,
      0x00, 0x00, 0x40, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x01, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0xff,
  };

  std::unique_ptr<ElfLib> elf = ElfLib::Create(kData, 84);

  ASSERT_NE(elf.get(), nullptr);
  EXPECT_FALSE(elf->ProbeHasDebugInfo());
  EXPECT_FALSE(elf->ProbeHasProgramBits());
  EXPECT_EQ(nullptr, elf->GetSectionData("bogus").ptr);
  EXPECT_EQ(0u, elf->GetSegmentHeaders().size());
  EXPECT_FALSE(elf->GetAllSymbols());
  EXPECT_FALSE(elf->GetAllDynamicSymbols());
  EXPECT_EQ(0u, elf->GetPLTOffsets().size());

  auto warnings = elf->GetAndClearWarnings();
  ASSERT_EQ(1u, warnings.size());
  EXPECT_EQ("Architecture doesn't support GetPLTOffsets.", warnings[0]);
}

// Checks that we can load a library which has a big plt (535 entries) on AArch64.
TEST(ElfLib, AArch64Plt) {
  std::unique_ptr<ElfLib> elf =
      ElfLib::Create(GetTestBinaryPath(std::string("6d4d8ac190ecc7.debug")));

  ASSERT_NE(elf.get(), nullptr);

  auto plt = elf->GetPLTOffsets();
  auto warnings = elf->GetAndClearWarnings();
  for (const auto& warning : warnings) {
    std::cout << warning << '\n';
  }
  ASSERT_EQ(plt.size(), 535U);
  ASSERT_NE(plt.find("_zx_channel_create"), plt.end());
  EXPECT_EQ(642864U, plt["_zx_channel_create"]);
  ASSERT_NE(plt.find("_zx_channel_read"), plt.end());
  EXPECT_EQ(651120U, plt["_zx_channel_read"]);
  ASSERT_NE(plt.find("_zx_channel_write"), plt.end());
  EXPECT_EQ(642848U, plt["_zx_channel_write"]);
  ASSERT_TRUE(warnings.empty());
}

}  // namespace elflib
