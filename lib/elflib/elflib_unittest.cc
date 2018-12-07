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

class TestMemoryAccessor : public ElfLib::MemoryAccessor {
 public:
  TestMemoryAccessor() {
    PushData(Elf64_Ehdr {
      .e_ident = { EI_MAG0, EI_MAG1, EI_MAG2, EI_MAG3 },
      .e_version = 1,
      .e_shoff = sizeof(Elf64_Ehdr),
      .e_ehsize = sizeof(Elf64_Ehdr),
      .e_shentsize = sizeof(Elf64_Shdr),
      .e_shnum = 4,
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

    DataAt<Elf64_Shdr>(shstrtab_hdr)->sh_offset = content_.size();
    const uint8_t* shstrtab_content =
      reinterpret_cast<const uint8_t*>(
        "\0.shstrtab\0.stuff\0.strtab\0.symtab\0");
    std::copy(shstrtab_content, shstrtab_content + 34,
              std::back_inserter(content_));

    DataAt<Elf64_Shdr>(stuff_hdr)->sh_offset = content_.size();
    const uint8_t* stuff_content =
      reinterpret_cast<const uint8_t*>("This is a test.");
    std::copy(stuff_content, stuff_content + 15,
              std::back_inserter(content_));

    DataAt<Elf64_Shdr>(strtab_hdr)->sh_offset = content_.size();
    const uint8_t* strtab_content =
      reinterpret_cast<const uint8_t*>("\0zx_frob_handle\0");
    std::copy(strtab_content, strtab_content + 16,
              std::back_inserter(content_));

    DataAt<Elf64_Shdr>(symtab_hdr)->sh_offset = content_.size();
    PushData(Elf64_Sym {
      .st_name = 1,
      .st_shndx = SHN_COMMON,
      .st_value = kSymbolPoison,
      .st_size = 0,
    });
  }

  template<typename T>
  T* DataAt(size_t offset) {
    return reinterpret_cast<T*>(content_.data() + offset);
  }

  template<typename T>
  size_t PushData(T data) {
    uint8_t* start = reinterpret_cast<uint8_t*>(&data);
    uint8_t* end = start + sizeof(data);
    size_t offset = content_.size();

    std::copy(start, end, std::back_inserter(content_));

    return offset;
  }

  bool GetMemory(uint64_t offset, std::vector<uint8_t>* out) {
    const auto& start_iter = content_.begin() + offset;

    if (content_.size() < offset + out->size()) {
      return false;
    }

    std::copy(start_iter, start_iter + out->size(), out->begin());
    return true;
  }

  bool GetMappedMemory(uint64_t offset, uint64_t address,
                       std::vector<uint8_t>* out) {
    if (address != kAddrPoison) {
      return false;
    }

    return GetMemory(offset, out);
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

}  // namespace elflib
