// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/self.h>

#include <zxtest/zxtest.h>

namespace {

extern "C" const char __ehdr_start [[gnu::visibility("hidden")]];

using Self = elfldltl::Self<>;

TEST(ElfldltlSelfTests, Valid) {
  EXPECT_TRUE(Self::Match());
  EXPECT_TRUE(Self::Valid());
}

TEST(ElfldltlSelfTests, LoadBias) {
  uintptr_t bias = Self::LoadBias();
#ifdef __PIE__
  EXPECT_EQ(bias, reinterpret_cast<uintptr_t>(&__ehdr_start));
#else
  EXPECT_EQ(bias, uintptr_t{0});
#endif
}

TEST(ElfldltlSelfTests, Phdrs) {
  auto phdrs = Self::Phdrs();
  EXPECT_GT(phdrs.size(), size_t{2});

  size_t loads = 0;
  size_t interps = 0;
  for (const auto& phdr : phdrs) {
    switch (phdr.type) {
      case elfldltl::ElfPhdrType::kLoad:
        ++loads;
        break;
      case elfldltl::ElfPhdrType::kInterp:
        ++interps;
        break;
      default:
        break;
    }
  }
  EXPECT_GT(loads, size_t{1});
  EXPECT_EQ(interps, size_t{1});
}

TEST(ElfldltlSelfTests, Dynamic) {
  size_t count = 0;
  bool symtab = false, strtab = false;
  for (const auto& dyn : Self::Dynamic()) {
    ++count;
    if (dyn.tag == elfldltl::ElfDynTag::kNull) {
      break;
    }
    switch (dyn.tag) {
      case elfldltl::ElfDynTag::kSymTab:
        symtab = true;
        EXPECT_NE(dyn.val(), 0);
        break;
      case elfldltl::ElfDynTag::kStrTab:
        strtab = true;
        EXPECT_NE(dyn.val(), 0);
        break;
      default:
        break;
    }
  }
  EXPECT_GT(count, size_t{3});
  EXPECT_TRUE(symtab);
  EXPECT_TRUE(strtab);
}

TEST(ElfldltlSelfTests, VisitSelf) {
  EXPECT_TRUE(elfldltl::VisitSelf([](auto&& self) { return self.Match(); }));
  EXPECT_TRUE(elfldltl::VisitSelf([](auto&& self) { return self.Valid(); }));
}

TEST(ElfldltlSelfTests, Memory) {
  auto memory = Self::Memory();
  const auto bias = Self::LoadBias();

  static const int something_in_memory = 0x12345678;

  const auto rodata_addr = reinterpret_cast<uintptr_t>(&something_in_memory);
  auto array = memory.ReadArray<int>(rodata_addr - bias, 1);
  ASSERT_TRUE(array.has_value());
  EXPECT_EQ(&something_in_memory, array->data());

  // The stack is not part of the load image, so this should be out of bounds.
  int something_on_stack = 0xabcdef;
  const auto stack_addr = reinterpret_cast<uintptr_t>(&something_on_stack);
  EXPECT_FALSE(memory.ReadArray<int>(stack_addr - bias, 1));
  EXPECT_FALSE(memory.ReadArray<int>(stack_addr - bias));
  EXPECT_FALSE(memory.Store<int>(stack_addr - bias, 2));
  EXPECT_FALSE(memory.StoreAdd<int>(stack_addr - bias, 3));
  EXPECT_EQ(0xabcdef, something_on_stack);

  // Set an initial value for a data word that will be overwritten by Store.
  static int mutable_in_memory;
  mutable_in_memory = 0xbad;

  // This cast to integer for the Memory API makes the compiler lose track of
  // the pointer identity here so it doesn't see how it's aliasable by the
  // Memory::Store code when that's fully inlined and so can move the store
  // later--such that EXPECT_EQ can fail and then print the matching value!
  // This barrier ensures the store is complete before the pointer is used.
  const auto mutable_addr = reinterpret_cast<uintptr_t>(&mutable_in_memory);
  std::atomic_signal_fence(std::memory_order_seq_cst);

  EXPECT_TRUE(memory.Store<int>(mutable_addr - bias, 0x12340000));
  EXPECT_EQ(0x12340000, mutable_in_memory);
  EXPECT_TRUE(memory.StoreAdd<int>(mutable_addr - bias, 0x5678));
  EXPECT_EQ(0x12345678, mutable_in_memory);
}

}  // namespace
