// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/constants.h>
#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/phdr.h>
#include <lib/stdcompat/span.h>

#include <optional>
#include <string>
#include <vector>

#include <zxtest/zxtest.h>

#include "tests.h"

namespace {

using elfldltl::ElfPhdrType;

// Diagnostic flags for signaling as much information as possible.
constexpr elfldltl::DiagnosticsFlags kFlags = {
    .multiple_errors = true,
    .strict = false,
    .extra_checking = true,
};

constexpr std::string_view kNullWarning = "PT_NULL header encountered";

constexpr auto EmptyTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  // No matchers and nothing to match.
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span<const Phdr>{}));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
};

TEST(ElfldltlPhdrTests, Empty) { TestAllFormats(EmptyTest); }

// No PT_NULL headers.
constexpr auto NullObserverNoNullsTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {{.type = ElfPhdrType::kLoad}};

  std::vector<std::string> warnings;
  auto diag = elfldltl::CollectStringsDiagnostics(warnings, kFlags);
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs), elfldltl::PhdrNullObserver<Elf>()));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
};

TEST(ElfldltlPhdrTests, NullObserverNoNulls) { TestAllFormats(NullObserverNoNullsTest); }

// One PT_NULL header.
constexpr auto NullObserverOneNullTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {
      {.type = ElfPhdrType::kLoad},
      {.type = ElfPhdrType::kNull},
      {.type = ElfPhdrType::kLoad},
  };

  std::vector<std::string> warnings;
  auto diag = elfldltl::CollectStringsDiagnostics(warnings, kFlags);
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs), elfldltl::PhdrNullObserver<Elf>()));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(1, diag.warnings());
  ASSERT_EQ(1, warnings.size());
  EXPECT_STR_EQ(kNullWarning, warnings[0]);
};

TEST(ElfldltlPhdrTests, NullObserverOneNull) { TestAllFormats(NullObserverOneNullTest); }

// Three PT_NULL headers.
constexpr auto NullObserverThreeNullsTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {
      {.type = ElfPhdrType::kNull}, {.type = ElfPhdrType::kNull}, {.type = ElfPhdrType::kLoad},
      {.type = ElfPhdrType::kNull}, {.type = ElfPhdrType::kLoad},
  };

  std::vector<std::string> warnings;
  auto diag = elfldltl::CollectStringsDiagnostics(warnings, kFlags);
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs), elfldltl::PhdrNullObserver<Elf>()));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(3, diag.warnings());
  ASSERT_EQ(3, warnings.size());
  EXPECT_STR_EQ(kNullWarning, warnings[0]);
  EXPECT_STR_EQ(kNullWarning, warnings[1]);
  EXPECT_STR_EQ(kNullWarning, warnings[2]);
};

TEST(ElfldltlPhdrTests, NullObserverThreeNulls) { TestAllFormats(NullObserverThreeNullsTest); }

}  // namespace
