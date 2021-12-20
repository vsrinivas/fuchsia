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

// At most one header per type.
constexpr auto SingletonObserverAtMostOneHeaderPerTypeTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {
      {.type = ElfPhdrType::kInterp},
      {.type = ElfPhdrType::kEhFrameHdr},
      {.type = ElfPhdrType::kRelro},
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors);
  std::optional<Phdr> dynamic, interp, eh_frame, relro;
  EXPECT_TRUE(elfldltl::DecodePhdrs(
      diag, cpp20::span(kPhdrs),  //
      elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kDynamic>(dynamic),
      elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kInterp>(interp),
      elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kEhFrameHdr>(eh_frame),
      elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kRelro>(relro)));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());

  EXPECT_FALSE(dynamic);

  ASSERT_TRUE(interp);
  EXPECT_EQ(ElfPhdrType::kInterp, interp->type);

  ASSERT_TRUE(eh_frame);
  EXPECT_EQ(ElfPhdrType::kEhFrameHdr, eh_frame->type);

  ASSERT_TRUE(relro);
  EXPECT_EQ(ElfPhdrType::kRelro, relro->type);
};

TEST(ElfldltlPhdrTests, SingletonObserverAtMostOneHeaderPerType) {
  TestAllFormats(SingletonObserverAtMostOneHeaderPerTypeTest);
}

// Multiple headers per type.
constexpr auto SingletonObserverMultipleHeadersPerTypeTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {
      {.type = ElfPhdrType::kInterp}, {.type = ElfPhdrType::kEhFrameHdr},
      {.type = ElfPhdrType::kRelro},  {.type = ElfPhdrType::kRelro},
      {.type = ElfPhdrType::kInterp},
  };

  std::vector<std::string> warnings;
  auto diag = elfldltl::CollectStringsDiagnostics(warnings, kFlags);
  std::optional<Phdr> interp, eh_frame, relro;
  EXPECT_TRUE(elfldltl::DecodePhdrs(
      diag, cpp20::span(kPhdrs),  //
      elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kInterp>(interp),
      elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kEhFrameHdr>(eh_frame),
      elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kRelro>(relro)));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(2, diag.warnings());

  ASSERT_EQ(warnings.size(), 2);
  EXPECT_STR_EQ(warnings[0], "too many PT_GNU_RELRO headers; expected at most one");
  EXPECT_STR_EQ(warnings[1], "too many PT_INTERP headers; expected at most one");
};

TEST(ElfldltlPhdrTests, SingletonObserverMultipleHeadersPerType) {
  TestAllFormats(SingletonObserverMultipleHeadersPerTypeTest);
}

}  // namespace
