// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/elflib/elflib.h"

#include <algorithm>
#include <iterator>

#include "gtest/gtest.h"

namespace elflib {
namespace {

class TestMemoryAccessor : public ElfLib::MemoryAccessor {
 public:
  TestMemoryAccessor() {
    PushData(Elf64_Ehdr {
      .e_ident = { EI_MAG0, EI_MAG1, EI_MAG2, EI_MAG3 },
      .e_version = 1,
      .e_shoff = sizeof(Elf64_Ehdr),
      .e_ehsize = sizeof(Elf64_Ehdr),
      .e_shentsize = sizeof(Elf64_Shdr),
      .e_shnum = 2,
      .e_shstrndx = 0,
    });

    size_t shsymtab_hdr = PushData(Elf64_Shdr {
      .sh_name = 1,
      .sh_type = SHT_SYMTAB,
      .sh_size = 18,
    });
    size_t stuff_hdr = PushData(Elf64_Shdr {
      .sh_name = 2,
      .sh_type = SHT_LOUSER,
      .sh_size = 15,
    });

    DataAt<Elf64_Shdr>(shsymtab_hdr)->sh_offset = content_.size();
    const uint8_t* shsymtab_content =
      reinterpret_cast<const uint8_t*>("\0.shsymtab\0.stuff\0");
    std::copy(shsymtab_content, shsymtab_content + 18,
              std::back_inserter(content_));

    DataAt<Elf64_Shdr>(stuff_hdr)->sh_offset = content_.size();
    const uint8_t* stuff_content =
      reinterpret_cast<const uint8_t*>("This is a test.");
    std::copy(stuff_content, stuff_content + 15,
              std::back_inserter(content_));
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

  auto data = elf->GetSectionData<uint8_t>(".stuff");
  const uint8_t* expected_content =
    reinterpret_cast<const uint8_t*>("This is a test.");
  auto test = std::vector<uint8_t>(expected_content, expected_content + 15);

  ASSERT_NE(data, nullptr);
  EXPECT_EQ(test, *data);
}

}  // namespace elflib
