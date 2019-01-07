// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/elflib/elflib.h"

#include <algorithm>
#include <iterator>

#include "gtest/gtest.h"

namespace elflib {
namespace {

constexpr uint64_t kAddrPoison = 0xdeadb33ff00db4b3;
constexpr uint64_t kSymbolPoison = 0xb0bab0ba;
constexpr uint64_t kNoteGnuBuildId = 3;
constexpr uint64_t kMeaninglessNoteType = 42;

class TestMemoryAccessor : public ElfLib::MemoryAccessor {
 public:
  TestMemoryAccessor() {
    PushData(Elf64_Ehdr {
      .e_ident = { EI_MAG0, EI_MAG1, EI_MAG2, EI_MAG3 },
      .e_version = 1,
      .e_shoff = sizeof(Elf64_Ehdr),
      .e_ehsize = sizeof(Elf64_Ehdr),
      .e_shentsize = sizeof(Elf64_Shdr),
      .e_phentsize = sizeof(Elf64_Phdr),
      .e_shnum = 4,
      .e_phnum = 2,
      .e_shstrndx = 0,
    });

    size_t shstrtab_hdr = PushData(Elf64_Shdr {
      .sh_name = 1,
      .sh_type = SHT_STRTAB,
      .sh_size = 34,
      .sh_addr = kAddrPoison,
    });
    size_t stuff_hdr = PushData(Elf64_Shdr {
      .sh_name = 2,
      .sh_type = SHT_LOUSER,
      .sh_size = 15,
      .sh_addr = kAddrPoison,
    });
    size_t strtab_hdr = PushData(Elf64_Shdr {
      .sh_name = 3,
      .sh_type = SHT_STRTAB,
      .sh_size = 16,
      .sh_addr = kAddrPoison,
    });
    size_t symtab_hdr = PushData(Elf64_Shdr {
      .sh_name = 4,
      .sh_type = SHT_SYMTAB,
      .sh_size = sizeof(Elf64_Sym),
      .sh_addr = kAddrPoison,
    });

    size_t phnote_hdr = PushData(Elf64_Phdr {
      .p_type = PT_NOTE,
      .p_vaddr = kAddrPoison,
    });
    DataAt<Elf64_Ehdr>(0)->e_phoff = phnote_hdr;

    DataAt<Elf64_Shdr>(shstrtab_hdr)->sh_offset =
      PushData("\0.shstrtab\0.stuff\0.strtab\0.symtab\0", 34);

    DataAt<Elf64_Shdr>(stuff_hdr)->sh_offset = PushData("This is a test.", 15);

    DataAt<Elf64_Shdr>(strtab_hdr)->sh_offset =
      PushData("\0zx_frob_handle\0", 16);

    DataAt<Elf64_Shdr>(symtab_hdr)->sh_offset = PushData(Elf64_Sym {
      .st_name = 1,
      .st_shndx = SHN_COMMON,
      .st_value = kSymbolPoison,
      .st_size = 0,
    });

    size_t buildid_nhdr = PushData(Elf64_Nhdr {
      .n_namesz = 4,
      .n_descsz = 32,
      .n_type = kNoteGnuBuildId
    });

    DataAt<Elf64_Phdr>(phnote_hdr)->p_offset = buildid_nhdr;

    PushData("GNU\0", 4);

    uint8_t desc_data[32] = {
      0, 1, 2, 3, 4, 5, 6, 7,
      0, 1, 2, 3, 4, 5, 6, 7,
      0, 1, 2, 3, 4, 5, 6, 7,
      0, 1, 2, 3, 4, 5, 6, 7,
    };

    PushData(desc_data, 32);

    PushData(Elf64_Nhdr {
      .n_namesz = 6,
      .n_descsz = 3,
      .n_type = kMeaninglessNoteType,
    });

    PushData("seven\0\0\0", 8);
    PushData("foo\0", 4);

    DataAt<Elf64_Phdr>(phnote_hdr)->p_filesz = Pos() - buildid_nhdr;
    DataAt<Elf64_Phdr>(phnote_hdr)->p_memsz = Pos() - buildid_nhdr;
  }

  template<typename T>
  T* DataAt(size_t offset) {
    return reinterpret_cast<T*>(content_.data() + offset);
  }

  template<typename T>
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

  size_t Pos() {
    return content_.size();
  }

  bool GetMemory(uint64_t offset, size_t size, std::vector<uint8_t>* out) {
    const auto& start_iter = content_.begin() + offset;
    out->clear();
    out->resize(size);

    if (content_.size() < offset + out->size()) {
      return false;
    }

    std::copy(start_iter, start_iter + out->size(), out->begin());
    return true;
  }

  bool GetMappedMemory(uint64_t offset, uint64_t address,
                       size_t size, size_t map_size,
                       std::vector<uint8_t>* out) {
    if (address != kAddrPoison) {
      return false;
    }

    return GetMemory(offset, size, out);
  }

 private:
  std::vector<uint8_t> content_;
};

}  // namespace

TEST(ElfLib, Create) {
  std::unique_ptr<ElfLib> got;

  EXPECT_NE(ElfLib::Create(std::make_unique<TestMemoryAccessor>()).get(),
            nullptr);
}

TEST(ElfLib, GetSection) {
  std::unique_ptr<ElfLib> elf =
    ElfLib::Create(std::make_unique<TestMemoryAccessor>());

  ASSERT_NE(elf.get(), nullptr);

  auto data = elf->GetSectionData(".stuff");
  const uint8_t* expected_content =
    reinterpret_cast<const uint8_t*>("This is a test.");
  auto test = std::vector<uint8_t>(expected_content, expected_content + 15);

  ASSERT_NE(data, nullptr);
  EXPECT_EQ(test, *data);
}

TEST(ElfLib, GetSymbolValue) {
  std::unique_ptr<ElfLib> elf =
    ElfLib::Create(std::make_unique<TestMemoryAccessor>());

  ASSERT_NE(elf.get(), nullptr);

  uint64_t data;
  ASSERT_TRUE(elf->GetSymbolValue("zx_frob_handle", &data));
  EXPECT_EQ(kSymbolPoison, data);
}

TEST(ElfLib, GetAllSymbols) {
  std::unique_ptr<ElfLib> elf =
    ElfLib::Create(std::make_unique<TestMemoryAccessor>());

  ASSERT_NE(elf.get(), nullptr);

  std::map<std::string,Elf64_Sym> syms;
  ASSERT_TRUE(elf->GetAllSymbols(&syms));
  EXPECT_EQ(1U, syms.size());

  Elf64_Sym sym = syms["zx_frob_handle"];
  EXPECT_EQ(1U, sym.st_name);
  EXPECT_EQ(0U, sym.st_size);
  EXPECT_EQ(SHN_COMMON, sym.st_shndx);
  EXPECT_EQ(kSymbolPoison, sym.st_value);
}

TEST(ElfLib, GetNote) {
  std::unique_ptr<ElfLib> elf =
    ElfLib::Create(std::make_unique<TestMemoryAccessor>());

  ASSERT_NE(elf.get(), nullptr);

  auto got = elf->GetNote("GNU", kNoteGnuBuildId);

  EXPECT_TRUE(got);
  auto data = *got;

  EXPECT_EQ(32U, data.size());

  for (size_t i = 0; i < 32; i++) {
    EXPECT_EQ(i % 8, data[i]);
  }
}

TEST(ElfLib, GetIrregularNote) {
  std::unique_ptr<ElfLib> elf =
    ElfLib::Create(std::make_unique<TestMemoryAccessor>());

  ASSERT_NE(elf.get(), nullptr);

  auto got = elf->GetNote("seven", kMeaninglessNoteType);

  EXPECT_TRUE(got);
  auto data = *got;

  EXPECT_EQ(3U, data.size());

  EXPECT_EQ("foo", std::string(data.data(), data.data() + 3));
}

}  // namespace elflib
