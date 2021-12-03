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

#include "tests.h"

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

}  // namespace
