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

}  // namespace
