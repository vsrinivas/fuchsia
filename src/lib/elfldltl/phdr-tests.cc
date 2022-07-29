// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/constants.h>
#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/memory.h>
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
    .warnings_are_errors = false,
    .extra_checking = true,
};

constexpr size_t kAlign = 0x1000;  // Example alignment.
constexpr size_t kPageSize = 0x1000;

constexpr std::string_view kNullWarning = "PT_NULL header encountered";

template <class Phdr>
constexpr auto kRWX = Phdr::kRead | Phdr::kWrite | Phdr::kExecute;

template <class Phdr>
constexpr Phdr OnePageStack(uint32_t flags) {
  Phdr phdr{.type = ElfPhdrType::kStack, .memsz = 0x1000};
  // Not inlined, as the relative ordering between `flags` and `memsz` is not
  // fixed.
  phdr.flags = flags;
  return phdr;
}

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
  EXPECT_STREQ(kNullWarning, warnings[0]);
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
  EXPECT_STREQ(kNullWarning, warnings[0]);
  EXPECT_STREQ(kNullWarning, warnings[1]);
  EXPECT_STREQ(kNullWarning, warnings[2]);
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
  EXPECT_STREQ(warnings[0], "too many PT_GNU_RELRO headers; expected at most one");
  EXPECT_STREQ(warnings[1], "too many PT_INTERP headers; expected at most one");
};

TEST(ElfldltlPhdrTests, SingletonObserverMultipleHeadersPerType) {
  TestAllFormats(SingletonObserverMultipleHeadersPerTypeTest);
}

constexpr auto UnknownFlagsTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {
      {.type = ElfPhdrType::kLoad, .flags = kRWX<Phdr>},
      {.type = ElfPhdrType::kDynamic, .flags = ~Phdr::kRead},
      {.type = ElfPhdrType::kInterp, .flags = ~Phdr::kWrite},
      {.type = ElfPhdrType::kStack, .flags = ~Phdr::kExecute},
      {.type = ElfPhdrType::kRelro, .flags = ~kRWX<Phdr>},
  };

  std::vector<std::string> warnings;
  auto diag = elfldltl::CollectStringsDiagnostics(warnings, kFlags);
  std::optional<Phdr> load, dynamic, interp, stack, relro;
  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),  //
                            elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kLoad>(load),
                            elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kDynamic>(dynamic),
                            elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kInterp>(interp),
                            elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kStack>(stack),
                            elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kRelro>(relro)));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(4, diag.warnings());

  ASSERT_EQ(warnings.size(), 4);
  EXPECT_STREQ(warnings[0],
               "PT_DYNAMIC header has unrecognized flags (other than PF_R, PF_W, PF_X)");
  EXPECT_STREQ(warnings[1],
               "PT_INTERP header has unrecognized flags (other than PF_R, PF_W, PF_X)");
  EXPECT_STREQ(warnings[2],
               "PT_GNU_STACK header has unrecognized flags (other than PF_R, PF_W, PF_X)");
  EXPECT_STREQ(warnings[3],
               "PT_GNU_RELRO header has unrecognized flags (other than PF_R, PF_W, PF_X)");
};

TEST(ElfldltlPhdrTests, UnknownFlags) { TestAllFormats(UnknownFlagsTest); }

constexpr auto BadAlignmentTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {
      {.type = ElfPhdrType::kLoad, .align = 0},          // OK
      {.type = ElfPhdrType::kDynamic, .align = kAlign},  // OK
      {.type = ElfPhdrType::kInterp, .align = 3},
      {.type = ElfPhdrType::kNote, .align = kAlign - 1},
      {.type = ElfPhdrType::kRelro, .align = kAlign + 1},
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);
  std::optional<Phdr> load, dynamic, interp, note, relro;
  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),  //
                            elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kLoad>(load),
                            elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kDynamic>(dynamic),
                            elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kInterp>(interp),
                            elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kNote>(note),
                            elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kRelro>(relro)));

  EXPECT_EQ(3, diag.errors());
  EXPECT_EQ(0, diag.warnings());

  ASSERT_EQ(errors.size(), 3);
  EXPECT_STREQ(errors[0], "PT_INTERP header has `p_align` that is not zero or a power of two");
  EXPECT_STREQ(errors[1], "PT_NOTE header has `p_align` that is not zero or a power of two");
  EXPECT_STREQ(errors[2], "PT_GNU_RELRO header has `p_align` that is not zero or a power of two");
};

TEST(ElfldltlPhdrTests, BadAlignment) { TestAllFormats(BadAlignmentTest); }

constexpr auto OffsetNotEquivVaddrTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {
      // OK
      {
          .type = ElfPhdrType::kLoad,
          .offset = kAlign,
          .vaddr = kAlign,
          .align = kAlign,
      },
      // OK
      {
          .type = ElfPhdrType::kDynamic,
          .offset = 17 * kAlign,
          .vaddr = kAlign,
          .align = kAlign,
      },
      // OK
      {
          .type = ElfPhdrType::kInterp,
          .offset = 100,
          .vaddr = 101,
          .align = 0,
      },
      {
          .type = ElfPhdrType::kNote,
          .offset = kAlign - 1,
          .vaddr = kAlign,
          .align = kAlign,
      },
      {
          .type = ElfPhdrType::kRelro,
          .offset = kAlign + 1,
          .vaddr = kAlign,
          .align = kAlign,
      }};

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);
  std::optional<Phdr> load, dynamic, interp, note, relro;
  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),  //
                            elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kLoad>(load),
                            elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kDynamic>(dynamic),
                            elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kInterp>(interp),
                            elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kNote>(note),
                            elfldltl::PhdrSingletonObserver<Elf, ElfPhdrType::kRelro>(relro)));

  EXPECT_EQ(2, diag.errors());
  EXPECT_EQ(0, diag.warnings());

  ASSERT_EQ(2, errors.size());
  EXPECT_STREQ(errors[0],
               "PT_NOTE header has incongruent `p_offset` and `p_vaddr` modulo `p_align`");
  EXPECT_STREQ(errors[1],
               "PT_GNU_RELRO header has incongruent `p_offset` and `p_vaddr` modulo `p_align`");
};

TEST(ElfldltlPhdrTests, OffsetNotEquivVaddrVaddr) { TestAllFormats(OffsetNotEquivVaddrTest); }

// Executable stack permitted; non-zero memsz.
constexpr auto StackObserverExecOkPhdrNonzeroSizeTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {OnePageStack<Phdr>(Phdr::kRead | Phdr::kWrite)};

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<size_type> size;
  bool executable = false;
  EXPECT_TRUE(elfldltl::DecodePhdrs(
      diag, cpp20::span(kPhdrs),
      elfldltl::PhdrStackObserver<Elf, /*CanBeExecutable=*/true>(size, executable)));

  ASSERT_TRUE(size);
  EXPECT_EQ(0x1000, *size);
};

TEST(ElfldltlPhdrTests, StackObserverExecOkPhdrNonzeroSize) {
  TestAllFormats(StackObserverExecOkPhdrNonzeroSizeTest);
}

// Executable stack permitted; zero memsz.
constexpr auto StackObserverExecOkPhdrZeroSizeTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {{.type = ElfPhdrType::kStack, .flags = Phdr::kRead | Phdr::kWrite}};

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<size_type> size;
  bool executable = false;
  EXPECT_TRUE(elfldltl::DecodePhdrs(
      diag, cpp20::span(kPhdrs),
      elfldltl::PhdrStackObserver<Elf, /*CanBeExecutable=*/true>(size, executable)));

  ASSERT_FALSE(size);
};

TEST(ElfldltlPhdrTests, StackObserverExecOkPhdrZeroSize) {
  TestAllFormats(StackObserverExecOkPhdrZeroSizeTest);
}

// Executable stack permitted; no header to report size.
constexpr auto StackObserverExecOkNoPhdrSizeTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Phdr = typename Elf::Phdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<size_type> size;
  bool executable = false;
  EXPECT_TRUE(elfldltl::DecodePhdrs(
      diag, cpp20::span<const Phdr>{},
      elfldltl::PhdrStackObserver<Elf, /*CanBeExecutable=*/true>(size, executable)));

  ASSERT_FALSE(size);
};

TEST(ElfldltlPhdrTests, StackObserverExecOkNoPhdrSize) {
  TestAllFormats(StackObserverExecOkNoPhdrSizeTest);
}

// Executable stack permitted; header present and reports PF_X.
constexpr auto StackObserverExecOkPhdrWithXTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {OnePageStack<Phdr>(kRWX<Phdr>)};

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<size_type> size;
  bool executable = false;
  EXPECT_TRUE(elfldltl::DecodePhdrs(
      diag, cpp20::span(kPhdrs),
      elfldltl::PhdrStackObserver<Elf, /*CanBeExecutable=*/true>(size, executable)));

  ASSERT_TRUE(size);
  EXPECT_EQ(0x1000, *size);
  EXPECT_TRUE(executable);
  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
};

TEST(ElfldltlPhdrTests, StackObserverExecOkPhdrWithX) {
  TestAllFormats(StackObserverExecOkPhdrWithXTest);
}

// Executable stack permitted; header present and does not report PF_X.
constexpr auto StackObserverExecOkPhdrWithoutXTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {OnePageStack<Phdr>(Phdr::kRead | Phdr::kWrite)};

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<size_type> size;
  bool executable = false;
  EXPECT_TRUE(elfldltl::DecodePhdrs(
      diag, cpp20::span(kPhdrs),
      elfldltl::PhdrStackObserver<Elf, /*CanBeExecutable=*/true>(size, executable)));

  EXPECT_FALSE(executable);
  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
};

TEST(ElfldltlPhdrTests, StackObserverExecOkPhdrWithoutX) {
  TestAllFormats(StackObserverExecOkPhdrWithoutXTest);
}

// Executable stack permitted; header not present.
constexpr auto StackObserverExecOkNoPhdrTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Phdr = typename Elf::Phdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<size_type> size;
  bool executable = false;
  EXPECT_TRUE(elfldltl::DecodePhdrs(
      diag, cpp20::span<const Phdr>{},
      elfldltl::PhdrStackObserver<Elf, /*CanBeExecutable=*/true>(size, executable)));

  EXPECT_TRUE(executable);
  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
};

TEST(ElfldltlPhdrTests, StackObserverExecOkNoPhdr) {
  TestAllFormats(StackObserverExecOkNoPhdrTest);
}

// Executable stack not permitted; non-zero memsz.
constexpr auto StackObserverExecNotOkPhdrNonzeroSizeTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {OnePageStack<Phdr>(Phdr::kRead | Phdr::kWrite)};

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<size_type> size;
  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                            elfldltl::PhdrStackObserver<Elf, /*CanBeExecutable=*/false>(size)));

  ASSERT_TRUE(size);
  EXPECT_EQ(0x1000, *size);
};

TEST(ElfldltlPhdrTests, StackObserverExecNotOkPhdrNonzeroSize) {
  TestAllFormats(StackObserverExecNotOkPhdrNonzeroSizeTest);
}

// Executable stack not permitted; zero memsz.
constexpr auto StackObserverExecNotOkPhdrZeroSizeTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {{.type = ElfPhdrType::kStack, .flags = Phdr::kRead | Phdr::kWrite}};

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<size_type> size;
  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                            elfldltl::PhdrStackObserver<Elf, /*CanBeExecutable=*/false>(size)));

  ASSERT_FALSE(size);
};

TEST(ElfldltlPhdrTests, StackObserverExecNotOkPhdrZeroSize) {
  TestAllFormats(StackObserverExecNotOkPhdrZeroSizeTest);
}

// Executable stack not permitted; no header to report size.
constexpr auto StackObserverExecNotOkNoPhdrSizeTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Phdr = typename Elf::Phdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<size_type> size;
  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span<const Phdr>{},
                            elfldltl::PhdrStackObserver<Elf, /*CanBeExecutable=*/false>(size)));

  ASSERT_FALSE(size);
};

TEST(ElfldltlPhdrTests, StackObserverExecNotOkNoPhdrSize) {
  TestAllFormats(StackObserverExecNotOkNoPhdrSizeTest);
}

// Executable stack not permitted; header present and reports PF_X.
constexpr auto StackObserverExecNotOkPhdrWithXTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {OnePageStack<Phdr>(kRWX<Phdr>)};

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<size_type> size;
  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                            elfldltl::PhdrStackObserver<Elf, /*CanBeExecutable=*/false>(size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(errors.size(), 1);
  EXPECT_STREQ(errors.front(), "executable stack not supported: PF_X is set");
};

TEST(ElfldltlPhdrTests, StackObserverExecNotOkPhdrWithX) {
  TestAllFormats(StackObserverExecNotOkPhdrWithXTest);
}

// Executable stack not permitted; header present and does not report PF_X.
constexpr auto StackObserverExecNotOkPhdrWithoutXTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {OnePageStack<Phdr>(Phdr::kRead | Phdr::kWrite)};

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<size_type> size;
  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                            elfldltl::PhdrStackObserver<Elf, /*CanBeExecutable=*/false>(size)));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
};

TEST(ElfldltlPhdrTests, StackObserverExecNotOkPhdrWithoutX) {
  TestAllFormats(StackObserverExecNotOkPhdrWithoutXTest);
}

// Executable stack not permitted; header not present.
constexpr auto StackObserverExecNotOkNoPhdrTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Phdr = typename Elf::Phdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<size_type> size;
  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span<const Phdr>{},
                            elfldltl::PhdrStackObserver<Elf, /*CanBeExecutable=*/false>(size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(errors.size(), 1);
  EXPECT_STREQ(errors.front(), "executable stack not supported: PT_GNU_STACK header required");
};

TEST(ElfldltlPhdrTests, StackObserverExecNotOkNoPhdr) {
  TestAllFormats(StackObserverExecNotOkNoPhdrTest);
}

// Non-readable stacks are disallowed.
constexpr auto StackObserverNonReadableTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {OnePageStack<Phdr>(Phdr::kWrite)};

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<size_type> size;
  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs), elfldltl::PhdrStackObserver<Elf>(size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(errors.size(), 1);
  EXPECT_STREQ(errors.front(), "stack is not readable: PF_R is not set");
};

TEST(ElfldltlPhdrTests, StackObserverNonReadable) { TestAllFormats(StackObserverNonReadableTest); }

// Non-writable stacks are disallowed.
constexpr auto StackObserverNonWritableTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {OnePageStack<Phdr>(Phdr::kRead)};

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<size_type> size;
  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs), elfldltl::PhdrStackObserver<Elf>(size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(errors.size(), 1);
  EXPECT_STREQ(errors.front(), "stack is not writable: PF_W is not set");
};

TEST(ElfldltlPhdrTests, StackObserverNonWritable) { TestAllFormats(StackObserverNonWritableTest); }

constexpr auto MetadataObserverNoPhdrTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<Phdr> phdr;
  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span<const Phdr>{},
                            elfldltl::PhdrMetadataObserver<Elf, ElfPhdrType::kInterp>(phdr)));

  EXPECT_FALSE(phdr);
  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
};

TEST(ElfldltlPhdrTests, MetadataObserverNoPhdr) { TestAllFormats(MetadataObserverNoPhdrTest); }

constexpr auto MetadataObserverUnalignedVaddrTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {
      {
          .type = ElfPhdrType::kInterp,
          .offset = kAlign + 1,
          .vaddr = kAlign + 1,
          .align = kAlign,
      },
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);
  std::optional<Phdr> phdr;
  EXPECT_TRUE(elfldltl::DecodePhdrs(
      diag, cpp20::span(kPhdrs), elfldltl::PhdrMetadataObserver<Elf, ElfPhdrType::kInterp>(phdr)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());

  ASSERT_EQ(errors.size(), 1);
  EXPECT_STREQ(errors[0], "PT_INTERP header has `p_vaddr % p_align != 0`");
};

TEST(ElfldltlPhdrTests, MetadataObserverUnalignedVaddr) {
  TestAllFormats(MetadataObserverUnalignedVaddrTest);
}

constexpr auto MetadataObserverFileszNotEqMemszTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  constexpr Phdr kPhdrs[] = {
      {
          .type = ElfPhdrType::kInterp,
          .filesz = kAlign,
          .memsz = kAlign + 1,
          .align = kAlign,
      },
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<Phdr> phdr;
  EXPECT_TRUE(elfldltl::DecodePhdrs(
      diag, cpp20::span(kPhdrs), elfldltl::PhdrMetadataObserver<Elf, ElfPhdrType::kInterp>(phdr)));

  EXPECT_TRUE(phdr);
  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(1, errors.size());
  EXPECT_STREQ("PT_INTERP header has `p_filesz != p_memsz`", errors.front());
};

TEST(ElfldltlPhdrTests, MetadataObserverFileszNotEqMemsz) {
  TestAllFormats(MetadataObserverFileszNotEqMemszTest);
}

constexpr auto MetadataObserverIncompatibleEntrySizeTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using Dyn = typename Elf::Dyn;

  constexpr Phdr kPhdrs[] = {
      {
          .type = ElfPhdrType::kDynamic,
          .filesz = sizeof(Dyn) + 1,
          .memsz = sizeof(Dyn) + 1,
          .align = kAlign,
      },
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<Phdr> phdr;
  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                            elfldltl::PhdrMetadataObserver<Elf, ElfPhdrType::kDynamic, Dyn>(phdr)));

  EXPECT_TRUE(phdr);
  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(1, errors.size());
  EXPECT_STREQ("PT_DYNAMIC segment size is not a multiple of entry size", errors.front());
};

TEST(ElfldltlPhdrTests, MetadataObserverIncompatibleEntrySize) {
  TestAllFormats(MetadataObserverIncompatibleEntrySizeTest);
}

constexpr auto MetadataObserverIncompatibleEntryAlignmentTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using Dyn = typename Elf::Dyn;

  constexpr Phdr kPhdrs[] = {
      {
          .type = ElfPhdrType::kDynamic,
          .align = alignof(Dyn) / 2,  // Too small.
      },
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  std::optional<Phdr> phdr;
  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                            elfldltl::PhdrMetadataObserver<Elf, ElfPhdrType::kDynamic, Dyn>(phdr)));

  EXPECT_TRUE(phdr);
  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(1, errors.size());
  EXPECT_STREQ("PT_DYNAMIC segment alignment is not a multiple of entry alignment", errors.front());
};

TEST(ElfldltlPhdrTests, MetadataObserverIncompatibleAlignmentSize) {
  TestAllFormats(MetadataObserverIncompatibleEntryAlignmentTest);
}

constexpr auto LoadObserverNoPhdrTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver = elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kBasic>;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span<const Phdr>{},
                                    LoadObserver(kPageSize, vaddr_start, vaddr_size)));

  EXPECT_EQ(0, vaddr_start);
  EXPECT_EQ(0, vaddr_size);
  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
};

TEST(ElfldltlPhdrTests, LoadObserverNoPhdr) { TestAllFormats(LoadObserverNoPhdrTest); }

constexpr auto BasicLoadObserverSmallAlignTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver = elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kBasic>;

  constexpr Phdr kPhdrs[] = {
      {.type = ElfPhdrType::kLoad, .memsz = kPageSize, .align = kPageSize / 2},
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kPageSize, vaddr_start, vaddr_size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(1, errors.size());
  EXPECT_STREQ("PT_LOAD's `p_align` is not page-aligned", errors.front());
};

TEST(ElfldltlPhdrTests, BasicLoadObserverSmallAlign) {
  TestAllFormats(BasicLoadObserverSmallAlignTest);
}

constexpr auto BasicLoadObserverZeroMemszTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver = elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kBasic>;

  constexpr Phdr kPhdrs[] = {
      {.type = ElfPhdrType::kLoad, .memsz = 0},
  };

  std::vector<std::string> warnings;
  auto diag = elfldltl::CollectStringsDiagnostics(warnings, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kPageSize, vaddr_start, vaddr_size)));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(1, diag.warnings());
  ASSERT_EQ(1, warnings.size());
  EXPECT_STREQ("PT_LOAD has `p_memsz == 0`", warnings.front());
};

TEST(ElfldltlPhdrTests, BasicLoadObserverZeroMemsz) {
  TestAllFormats(BasicLoadObserverZeroMemszTest);
}

constexpr auto BasicLoadObserverMemszTooSmallTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver = elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kBasic>;

  constexpr Phdr kPhdrs[] = {
      {.type = ElfPhdrType::kLoad, .filesz = 0x100, .memsz = 0x100 - 1},
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kPageSize, vaddr_start, vaddr_size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(1, errors.size());
  EXPECT_STREQ("PT_LOAD has `p_memsz < p_filesz`", errors.front());
};

TEST(ElfldltlPhdrTests, BasicLoadObserverMemszTooSmall) {
  TestAllFormats(BasicLoadObserverMemszTooSmallTest);
}

constexpr auto BasicLoadObserverMemEndOverflowTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver = elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kBasic>;

  constexpr auto kMax = std::numeric_limits<size_type>::max();

  constexpr Phdr kPhdrs[] = {
      {.type = ElfPhdrType::kLoad, .vaddr = kAlign, .memsz = kMax},
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kPageSize, vaddr_start, vaddr_size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(1, errors.size());
  EXPECT_STREQ("PT_LOAD has overflowing `p_vaddr + p_memsz`", errors.front());
};

TEST(ElfldltlPhdrTests, BasicLoadObserverMemEndOverflow) {
  TestAllFormats(BasicLoadObserverMemEndOverflowTest);
}

constexpr auto BasicLoadObserverAlignedMemEndOverflowTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver = elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kBasic>;

  constexpr auto kMax = std::numeric_limits<size_type>::max();

  constexpr Phdr kPhdrs[] = {
      {.type = ElfPhdrType::kLoad, .vaddr = 0, .memsz = kMax - kAlign + 2, .align = kAlign},
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kPageSize, vaddr_start, vaddr_size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(1, errors.size());
  EXPECT_STREQ("PT_LOAD has overflowing `p_align`-aligned `p_vaddr + p_memsz`", errors.front());
};

TEST(ElfldltlPhdrTests, BasicLoadObserverAlignedMemEndOverflows) {
  TestAllFormats(BasicLoadObserverAlignedMemEndOverflowTest);
}

constexpr auto BasicLoadObserverFileEndOverflowTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver = elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kBasic>;

  constexpr auto kMax = std::numeric_limits<size_type>::max();

  constexpr Phdr kPhdrs[] = {
      {
          .type = ElfPhdrType::kLoad,
          .offset = 2 * kAlign,
          .filesz = kMax - kAlign,
          .memsz = kMax - kAlign,
          .align = kAlign,
      },
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kPageSize, vaddr_start, vaddr_size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(1, errors.size());
  EXPECT_STREQ("PT_LOAD has overflowing `p_offset + p_filesz`", errors.front());
};

TEST(ElfldltlPhdrTests, BasicLoadObserverFileEndOverflow) {
  TestAllFormats(BasicLoadObserverFileEndOverflowTest);
}

constexpr auto BasicLoadObserverAlignedFileEndOverflowTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver = elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kBasic>;

  constexpr auto kMax = std::numeric_limits<size_type>::max();

  constexpr Phdr kPhdrs[] = {
      {
          .type = ElfPhdrType::kLoad,
          .offset = 2 * kAlign,
          .filesz = kMax - 3 * kAlign + 2,
          .memsz = kMax - 3 * kAlign + 2,
          .align = kAlign,
      },
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kPageSize, vaddr_start, vaddr_size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(1, errors.size());
  EXPECT_STREQ("PT_LOAD has overflowing `p_align`-aligned `p_offset + p_filesz`", errors.front());
};

TEST(ElfldltlPhdrTests, BasicLoadObserverAlignedFileEndOverflows) {
  TestAllFormats(BasicLoadObserverAlignedFileEndOverflowTest);
}

constexpr auto BasicLoadObserverUnorderedTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver = elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kBasic>;

  constexpr Phdr kPhdrs[] = {
      {.type = ElfPhdrType::kLoad, .vaddr = kAlign, .memsz = kAlign, .align = kAlign},
      {.type = ElfPhdrType::kLoad, .vaddr = 3 * kAlign, .memsz = kAlign, .align = kAlign},
      {.type = ElfPhdrType::kLoad, .vaddr = 2 * kAlign, .memsz = kAlign, .align = kAlign},
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kPageSize, vaddr_start, vaddr_size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(1, errors.size());
  EXPECT_STREQ(
      "PT_LOAD has `p_align`-aligned memory ranges that overlap or do not increase "
      "monotonically",
      errors.front());
};

TEST(ElfldltlPhdrTests, BasicLoadObserverUnordered) {
  TestAllFormats(BasicLoadObserverUnorderedTest);
}

constexpr auto BasicLoadObserverOverlappingMemoryRangeTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver = elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kBasic>;

  constexpr Phdr kPhdrs[] = {
      {.type = ElfPhdrType::kLoad, .vaddr = kAlign, .memsz = 2 * kAlign},
      {.type = ElfPhdrType::kLoad, .vaddr = 2 * kAlign, .memsz = 2 * kAlign},
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kPageSize, vaddr_start, vaddr_size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(1, errors.size());
  EXPECT_STREQ(
      "PT_LOAD has `p_align`-aligned memory ranges that overlap or do not increase "
      "monotonically",
      errors.front());
};

TEST(ElfldltlPhdrTests, BasicLoadObserverOverlappingMemoryRange) {
  TestAllFormats(BasicLoadObserverOverlappingMemoryRangeTest);
}

constexpr auto BasicLoadObserverCompliantTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver = elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kBasic>;

  constexpr Phdr kPhdrs[] = {
      // [kAlign + 10, 2*kAlign + 10)
      {
          .type = ElfPhdrType::kLoad,
          .offset = 10,
          .vaddr = kAlign + 10,
          .memsz = kAlign,
          .align = kAlign,
      },
      // [3*kAlign, (7/2)*kAlign)
      {
          .type = ElfPhdrType::kLoad,
          .offset = kAlign,
          .vaddr = 3 * kAlign,
          .memsz = kAlign / 2,
          .align = kAlign,
      },
      // [(37/2)*kAlign, 100*kAlign - 10)
      {
          .type = ElfPhdrType::kLoad,
          .offset = kAlign / 2,
          .vaddr = 37 * (kAlign / 2),
          .memsz = 100 * kAlign - 10 - 37 * (kAlign / 2),
          .align = kAlign,
      },
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kAlign / 2, vaddr_start, vaddr_size)));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());

  EXPECT_EQ(kAlign, vaddr_start);
  EXPECT_EQ(99 * kAlign, vaddr_size);
};

TEST(ElfldltlPhdrTests, BasicLoadObserverCompliant) {
  TestAllFormats(BasicLoadObserverCompliantTest);
}

constexpr auto FileRangeMonotonicLoadObserverUnorderedTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver =
      elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kFileRangeMonotonic>;

  constexpr Phdr kPhdrs[] = {
      {
          .type = ElfPhdrType::kLoad,
          .offset = kPageSize,
          .vaddr = 0,
          .filesz = kPageSize,
          .memsz = kPageSize,
      },
      {
          .type = ElfPhdrType::kLoad,
          .offset = 3 * kPageSize,
          .vaddr = kPageSize,
          .filesz = kPageSize,
          .memsz = kPageSize,
      },
      {
          .type = ElfPhdrType::kLoad,
          .offset = 2 * kPageSize,
          .vaddr = 2 * kPageSize,
          .filesz = kPageSize,
          .memsz = kPageSize,
      },
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kPageSize, vaddr_start, vaddr_size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(1, errors.size());
  EXPECT_STREQ(
      "PT_LOAD has `p_align`-aligned file offset ranges that overlap or do not "
      "increase monotonically",
      errors.front());
};

TEST(ElfldltlPhdrTests, FileRangeMonotonicLoadObserverUnordered) {
  TestAllFormats(FileRangeMonotonicLoadObserverUnorderedTest);
}

constexpr auto FileRangeMonotonicLoadObserverOverlappingAlignedFileRangeTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver =
      elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kFileRangeMonotonic>;

  constexpr Phdr kPhdrs[] = {
      {
          .type = ElfPhdrType::kLoad,
          .offset = 0,
          .vaddr = 0,
          .filesz = 3 * (kAlign / 2),
          .memsz = 3 * (kAlign / 2),
          .align = kAlign,
      },
      {
          .type = ElfPhdrType::kLoad,
          .offset = 3 * (kAlign / 2),
          .vaddr = 5 * (kAlign / 2),
          .filesz = kAlign,
          .memsz = kAlign,
          .align = kAlign,
      },
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kAlign, vaddr_start, vaddr_size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(1, errors.size());
  EXPECT_STREQ(
      "PT_LOAD has `p_align`-aligned file offset ranges that overlap or do not "
      "increase monotonically",
      errors.front());
};

TEST(ElfldltlPhdrTests, FileRangeMonotonicLoadObserverOverlappingAlignedFileRange) {
  TestAllFormats(FileRangeMonotonicLoadObserverOverlappingAlignedFileRangeTest);
}

constexpr auto FileRangeMonotonicLoadObserverCompliantTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver =
      elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kFileRangeMonotonic>;

  constexpr Phdr kPhdrs[] = {
      // memory: [kAlign + 10, (3/2)*kAlign + 10)
      // file: [kAlign + 10, (3/2)*kAlign + 10)
      {
          .type = ElfPhdrType::kLoad,
          .offset = kAlign + 10,
          .vaddr = kAlign + 10,
          .filesz = kAlign / 2,
          .memsz = kAlign / 2,
          .align = kAlign,
      },
      // memory: [3*kAlign, (7/2)*kAlign)
      // file: [2*kAlign, 3*kAlign)
      {
          .type = ElfPhdrType::kLoad,
          .offset = 2 * kAlign,
          .vaddr = 3 * kAlign,
          .filesz = kAlign / 2,
          .memsz = kAlign / 2,
          .align = kAlign,
      },
      // memory: [(37/2)*kAlign - 100, 100*kAlign - 10)
      // file: [(21/2)*kAlign - 100, 11*kAlign - 10)
      {
          .type = ElfPhdrType::kLoad,
          .offset = 21 * (kAlign / 2) - 100,
          .vaddr = 37 * (kAlign / 2) - 100,
          .filesz = 11 * kAlign - 10 - 21 * (kAlign / 2) + 100,
          .memsz = 100 * kAlign - 10 - 37 * (kAlign / 2) + 100,
          .align = kAlign,
      },
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kAlign, vaddr_start, vaddr_size)));

  // EXPECT_STREQ("", errors.front());
  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  EXPECT_EQ(kAlign, vaddr_start);
  EXPECT_EQ(99 * kAlign, vaddr_size);
};

TEST(ElfldltlPhdrTests, FileRangeMonotonicLoadObserverCompliant) {
  TestAllFormats(FileRangeMonotonicLoadObserverCompliantTest);
}

constexpr auto ContiguousLoadObserverHighFirstOffsetTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver = elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kContiguous>;

  constexpr Phdr kPhdrs[] = {
      {
          .type = ElfPhdrType::kLoad,
          .offset = 0,
          .vaddr = kAlign,
          .filesz = kAlign,
          .memsz = kAlign,
          .align = kAlign,
      },
      {
          .type = ElfPhdrType::kLoad,
          .offset = 3 * kAlign,
          .vaddr = 2 * kAlign,
          .filesz = kAlign,
          .memsz = kAlign,
          .align = kAlign,
      },
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kAlign, vaddr_start, vaddr_size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(1, errors.size());
  EXPECT_STREQ("PT_LOAD has `p_align`-aligned file offset ranges that are not contiguous",
               errors.front());
};

TEST(ElfldltlPhdrTests, ContiguousLoadObserverHighFirstOffset) {
  TestAllFormats(ContiguousLoadObserverHighFirstOffsetTest);
}

constexpr auto ContiguousLoadObserverNonContiguousFileRangesTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver = elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kContiguous>;

  constexpr Phdr kPhdrs[] = {
      {
          .type = ElfPhdrType::kLoad,
          .offset = kPageSize,
          .vaddr = kPageSize,
          .filesz = kPageSize,
          .memsz = kPageSize,
      },
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kPageSize, vaddr_start, vaddr_size)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_EQ(1, errors.size());
  EXPECT_STREQ("first PT_LOAD's `p_offset` does not lie within the first page", errors.front());
};

TEST(ElfldltlPhdrTests, ContiguousLoadObserverNonContiguousFileRanges) {
  TestAllFormats(ContiguousLoadObserverNonContiguousFileRangesTest);
}

constexpr auto ContiguousLoadObserverCompliantTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;
  using LoadObserver = elfldltl::PhdrLoadObserver<Elf, elfldltl::PhdrLoadPolicy::kContiguous>;

  constexpr Phdr kPhdrs[] = {
      // memory: [kAlign + 10, 2*kAlign + 10)
      // file: [10, kAlign)
      {
          .type = ElfPhdrType::kLoad,
          .offset = 10,
          .vaddr = kAlign + 10,
          .filesz = kAlign - 10,
          .memsz = kAlign,
          .align = kAlign,
      },
      // memory: [3*kAlign + 10, (9/2)*kAlign + 100)
      // file: [kAlign + 10, 2*kAlign - 1)
      {
          .type = ElfPhdrType::kLoad,
          .offset = kAlign + 10,
          .vaddr = 3 * kAlign + 10,
          .filesz = kAlign - 11,
          .memsz = 3 * (kAlign / 2) + 90,
          .align = kAlign,
      },
      // memory: [5*kAlign + 100, 6*kAlign + 100)
      // file: [2*kAlign + 100, 3*kAlign)
      {
          .type = ElfPhdrType::kLoad,
          .offset = 2 * kAlign + 100,
          .vaddr = 5 * kAlign + 100,
          .filesz = kAlign - 100,
          .memsz = kAlign,
          .align = kAlign,
      },
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;
  EXPECT_TRUE(elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs),
                                    LoadObserver(kAlign, vaddr_start, vaddr_size)));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  EXPECT_EQ(kAlign, vaddr_start);
  EXPECT_EQ(6 * kAlign, vaddr_size);
};

TEST(ElfldltlPhdrTests, ContiguousLoadObserverCompliant) {
  TestAllFormats(ContiguousLoadObserverCompliantTest);
}

constexpr auto LoadObserverCallbackTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;

  struct ExpectedLoad {
    size_type offset, filesz, memsz, limit;
  };
  constexpr std::array kExpected{
      ExpectedLoad{
          .offset = 0,
          .filesz = 1234,
          .memsz = 2345,
          .limit = kAlign,
      },
      ExpectedLoad{
          .offset = kAlign,
          .filesz = 2345,
          .memsz = 3456,
          .limit = 2 * kAlign,
      },
  };

  constexpr Phdr kPhdrs[] = {
      {
          .type = ElfPhdrType::kInterp,
          .offset = 1200,
          .filesz = 17,
          .memsz = 17,
      },
      {
          .type = ElfPhdrType::kLoad,
          .offset = kExpected[0].offset,
          .vaddr = 0,
          .filesz = kExpected[0].filesz,
          .memsz = kExpected[0].memsz,
          .align = kAlign,
      },
      {
          .type = ElfPhdrType::kLoad,
          .offset = kExpected[1].offset,
          .vaddr = kAlign,
          .filesz = kExpected[1].filesz,
          .memsz = kExpected[1].memsz,
          .align = kAlign,
      },
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = 0;
  size_type vaddr_size = 0;

  size_t count = 0;
  auto check_phdrs = [kExpected, &count, &diag](auto& callback_diag, const Phdr& phdr) -> bool {
    EXPECT_EQ(&callback_diag, &diag);

    // We only get callbacks for the PT_LOAD headers, not the others.
    EXPECT_EQ(phdr.type, ElfPhdrType::kLoad);

    EXPECT_LT(count, std::size(kExpected));
    if (count >= std::size(kExpected)) {
      return false;
    }

    EXPECT_EQ(phdr.offset(), kExpected[count].offset, "#%zu", count);
    EXPECT_EQ(phdr.filesz(), kExpected[count].filesz, "#%zu", count);
    EXPECT_EQ(phdr.memsz(), kExpected[count].memsz, "#%zu", count);

    ++count;
    return true;
  };

  EXPECT_TRUE(elfldltl::DecodePhdrs(
      diag, cpp20::span(kPhdrs),
      elfldltl::MakePhdrLoadObserver<Elf>(kPageSize, vaddr_start, vaddr_size, check_phdrs)));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  EXPECT_EQ(0, vaddr_start);
  EXPECT_EQ(kAlign * 2, vaddr_size);

  ASSERT_EQ(count, 2);
};

TEST(ElfldltlPhdrTests, LoadObserverCallback) { TestAllFormats(LoadObserverCallbackTest); }

constexpr auto LoadObserverCallbackBailoutTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;

  constexpr Phdr kPhdrs[] = {
      {
          .type = ElfPhdrType::kInterp,
          .offset = 1200,
          .filesz = 17,
          .memsz = 17,
      },
      {
          .type = ElfPhdrType::kLoad,
          .offset = 0,
          .vaddr = 0,
          .filesz = 1234,
          .memsz = 1234,
          .align = kAlign,
      },
      {
          .type = ElfPhdrType::kLoad,
          .offset = 0,
          .vaddr = kAlign,
          .filesz = 1234,
          .memsz = 2345,
          .align = kAlign,
      },
  };

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  size_type vaddr_start = -1;
  size_type vaddr_size = 0;

  auto bail_early = [](auto& diag, const Phdr& phdr) {
    EXPECT_EQ(phdr.memsz(), 1234);
    return false;
  };
  EXPECT_FALSE(elfldltl::DecodePhdrs(
      diag, cpp20::span(kPhdrs),
      elfldltl::MakePhdrLoadObserver<Elf>(kPageSize, vaddr_start, vaddr_size, bail_early)));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  EXPECT_EQ(0, vaddr_start);

  // It should have bailed out on seeing the first PT_LOAD, but only after the
  // generic code updated the vaddr_size.  It's still before the second PT_LOAD
  // gets processed, so the vaddr_size shouldn't have its final value yet.
  EXPECT_EQ(kAlign, vaddr_size);
};

TEST(ElfldltlPhdrTests, LoadObserverCallbackBailout) {
  TestAllFormats(LoadObserverCallbackBailoutTest);
}

constexpr auto ReadPhdrsFromFileBadSizeTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Ehdr = typename Elf::Ehdr;
  using Phdr = typename Elf::Phdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  Ehdr ehdr{.phentsize = sizeof(Phdr) + 1, .phnum = 1};
  elfldltl::DirectMemory file;
  auto result = ReadPhdrsFromFile(diag, file, elfldltl::NoArrayFromFile<Phdr>(), ehdr);

  EXPECT_EQ(1, diag.errors());
  ASSERT_EQ(1, errors.size());
  EXPECT_EQ("e_phentsize has unexpected value", errors[0]);
  EXPECT_EQ(0, diag.warnings());
  EXPECT_FALSE(result);
};

TEST(ElfldltlPhdrTests, ReadPhdrsFromFileBadSize) { TestAllFormats(ReadPhdrsFromFileBadSizeTest); }

constexpr auto ReadPhdrsFromFileBadOffsetTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Ehdr = typename Elf::Ehdr;
  using Phdr = typename Elf::Phdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  Ehdr ehdr{.phoff = 0, .phentsize = sizeof(Phdr), .phnum = 1};
  elfldltl::DirectMemory file;
  auto result = ReadPhdrsFromFile(diag, file, elfldltl::NoArrayFromFile<Phdr>(), ehdr);

  EXPECT_EQ(1, diag.errors());
  ASSERT_EQ(1, errors.size());
  EXPECT_EQ("e_phoff overlaps with ELF file header", errors[0]);
  EXPECT_EQ(0, diag.warnings());
  EXPECT_FALSE(result);
};

TEST(ElfldltlPhdrTests, ReadPhdrsFromFileBadOffset) {
  TestAllFormats(ReadPhdrsFromFileBadOffsetTest);
}

constexpr auto ReadPhdrsFromFileBadAlignTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Ehdr = typename Elf::Ehdr;
  using Phdr = typename Elf::Phdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  Ehdr ehdr{.phoff = sizeof(Ehdr) + 1, .phentsize = sizeof(Phdr), .phnum = 1};
  elfldltl::DirectMemory file;
  auto result = ReadPhdrsFromFile(diag, file, elfldltl::NoArrayFromFile<Phdr>(), ehdr);

  EXPECT_EQ(1, diag.errors());
  ASSERT_EQ(1, errors.size());
  EXPECT_EQ("e_phoff has insufficient alignment", errors[0]);
  EXPECT_EQ(0, diag.warnings());
  EXPECT_FALSE(result);
};

TEST(ElfldltlPhdrTests, ReadPhdrsFromFileBadAlign) {
  TestAllFormats(ReadPhdrsFromFileBadAlignTest);
}

constexpr auto ReadPhdrsFromFilePhXNumBadShSizeTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Ehdr = typename Elf::Ehdr;
  using Phdr = typename Elf::Phdr;
  using Shdr = typename Elf::Shdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  Ehdr ehdr{.phoff = sizeof(Ehdr),
            .phentsize = sizeof(Phdr),
            .phnum = Ehdr::kPnXnum,
            .shentsize = sizeof(Shdr) + 1,
            .shnum = 1};
  elfldltl::DirectMemory file;
  auto result = ReadPhdrsFromFile(diag, file, elfldltl::NoArrayFromFile<Phdr>(), ehdr);

  EXPECT_EQ(1, diag.errors());
  ASSERT_EQ(1, errors.size());
  EXPECT_EQ("e_shentsize has unexpected value", errors[0]);
  EXPECT_EQ(0, diag.warnings());
  EXPECT_FALSE(result);
};

TEST(ElfldltlPhdrTests, ReadPhdrsFromFilePhXNumBadShSize) {
  TestAllFormats(ReadPhdrsFromFilePhXNumBadShSizeTest);
}

constexpr auto ReadPhdrsFromFilePhXNumBadShOffTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Ehdr = typename Elf::Ehdr;
  using Phdr = typename Elf::Phdr;
  using Shdr = typename Elf::Shdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  Ehdr ehdr{.phoff = sizeof(Ehdr),
            .shoff = 0,
            .phentsize = sizeof(Phdr),
            .phnum = Ehdr::kPnXnum,
            .shentsize = sizeof(Shdr),
            .shnum = 1};
  elfldltl::DirectMemory file;
  auto result = ReadPhdrsFromFile(diag, file, elfldltl::NoArrayFromFile<Phdr>(), ehdr);

  EXPECT_EQ(1, diag.errors());
  ASSERT_EQ(1, errors.size());
  EXPECT_EQ("e_shoff overlaps with ELF file header", errors[0]);
  EXPECT_EQ(0, diag.warnings());
  EXPECT_FALSE(result);
};

TEST(ElfldltlPhdrTests, ReadPhdrsFromFilePhXNumBadShOff) {
  TestAllFormats(ReadPhdrsFromFilePhXNumBadShOffTest);
}

constexpr auto ReadPhdrsFromFilePhXNumNoShdrsTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Ehdr = typename Elf::Ehdr;
  using Phdr = typename Elf::Phdr;
  using Shdr = typename Elf::Shdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  Ehdr ehdr{.phoff = sizeof(Ehdr),
            .shoff = sizeof(Ehdr),
            .phentsize = sizeof(Phdr),
            .phnum = Ehdr::kPnXnum,
            .shentsize = sizeof(Shdr),
            .shnum = 0};
  elfldltl::DirectMemory file;
  auto result = ReadPhdrsFromFile(diag, file, elfldltl::NoArrayFromFile<Phdr>(), ehdr);

  EXPECT_EQ(1, diag.errors());
  ASSERT_EQ(1, errors.size());
  EXPECT_EQ("PN_XNUM with no section headers", errors[0]);
  EXPECT_EQ(0, diag.warnings());
  EXPECT_FALSE(result);
};

TEST(ElfldltlPhdrTests, ReadPhdrsFromFilePhXNumNoShdrs) {
  TestAllFormats(ReadPhdrsFromFilePhXNumNoShdrsTest);
}

constexpr auto ReadPhdrsFromFilePhXNumCantReadShdrTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Ehdr = typename Elf::Ehdr;
  using Phdr = typename Elf::Phdr;
  using Shdr = typename Elf::Shdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  Ehdr ehdr{.phoff = sizeof(Ehdr),
            .shoff = sizeof(Ehdr),
            .phentsize = sizeof(Phdr),
            .phnum = Ehdr::kPnXnum,
            .shentsize = sizeof(Shdr),
            .shnum = 1};
  elfldltl::DirectMemory file;
  auto result = ReadPhdrsFromFile(diag, file, elfldltl::NoArrayFromFile<Phdr>(), ehdr);

  EXPECT_EQ(1, diag.errors());
  ASSERT_EQ(1, errors.size());
  EXPECT_EQ("cannot read section header 0 from ELF file", errors[0]);
  EXPECT_EQ(0, diag.warnings());
  EXPECT_FALSE(result);
};

TEST(ElfldltlPhdrTests, ReadPhdrsFromFilePhXNumCantReadShdr) {
  TestAllFormats(ReadPhdrsFromFilePhXNumCantReadShdrTest);
}

constexpr auto ReadPhdrsFromFileCantReadPhdrTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Ehdr = typename Elf::Ehdr;
  using Phdr = typename Elf::Phdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  Ehdr ehdr{.phoff = sizeof(Ehdr), .phentsize = sizeof(Phdr), .phnum = 1};
  elfldltl::DirectMemory file;
  auto result = ReadPhdrsFromFile(diag, file, elfldltl::NoArrayFromFile<Phdr>(), ehdr);

  EXPECT_EQ(1, diag.errors());
  ASSERT_EQ(1, errors.size());
  EXPECT_EQ("cannot read program headers from ELF file", errors[0]);
  EXPECT_EQ(0, diag.warnings());
  EXPECT_FALSE(result);
};

TEST(ElfldltlPhdrTests, ReadPhdrsFromFileCantReadPhdr) {
  TestAllFormats(ReadPhdrsFromFileCantReadPhdrTest);
}

constexpr auto ReadPhdrsFromFileNoPhdrsTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Ehdr = typename Elf::Ehdr;
  using Phdr = typename Elf::Phdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  Ehdr ehdr{.phnum = 0};
  elfldltl::DirectMemory file;
  auto result = ReadPhdrsFromFile(diag, file, elfldltl::NoArrayFromFile<Phdr>(), ehdr);

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_TRUE(result);
  EXPECT_EQ(0, result->size());
};

TEST(ElfldltlPhdrTests, ReadPhdrsFromFileNoPhdrs) { TestAllFormats(ReadPhdrsFromFileNoPhdrsTest); }

constexpr auto ReadPhdrsFromFileTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Ehdr = typename Elf::Ehdr;
  using Phdr = typename Elf::Phdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  struct [[gnu::packed]] {
    Ehdr ehdr{.phoff = sizeof(Ehdr), .phentsize = sizeof(Phdr), .phnum = 1};
    Phdr phdrs[1]{};
  } elfbytes;
  elfldltl::DirectMemory file{
      cpp20::span<std::byte>{reinterpret_cast<std::byte*>(&elfbytes), sizeof(elfbytes)}};
  auto result = ReadPhdrsFromFile(diag, file, elfldltl::NoArrayFromFile<Phdr>(), elfbytes.ehdr);

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_TRUE(result);
  auto& phdrs = *result;
  EXPECT_EQ(1, phdrs.size());
  EXPECT_FALSE((memcmp(elfbytes.phdrs, std::addressof(phdrs[0]), sizeof(Phdr))));
};

TEST(ElfldltlPhdrTests, ReadPhdrsFromFile) { TestAllFormats(ReadPhdrsFromFileTest); }

constexpr auto ReadPhdrsFromFilePhXNumTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Ehdr = typename Elf::Ehdr;
  using Phdr = typename Elf::Phdr;
  using Shdr = typename Elf::Shdr;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kFlags);

  struct [[gnu::packed]] {
    Ehdr ehdr{.phoff = sizeof(Ehdr) + sizeof(Shdr),
              .shoff = sizeof(Ehdr),
              .phentsize = sizeof(Phdr),
              .phnum = Ehdr::kPnXnum,
              .shentsize = sizeof(Shdr),
              .shnum = 1};
    Shdr shdrs[1]{{.info = 1}};
    Phdr phdrs[1]{};
  } elfbytes;
  elfldltl::DirectMemory file{
      cpp20::span<std::byte>{reinterpret_cast<std::byte*>(&elfbytes), sizeof(elfbytes)}};
  auto result = ReadPhdrsFromFile(diag, file, elfldltl::NoArrayFromFile<Phdr>(), elfbytes.ehdr);

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  ASSERT_TRUE(result);
  auto& phdrs = *result;
  EXPECT_EQ(1, phdrs.size());
  EXPECT_FALSE((memcmp(elfbytes.phdrs, std::addressof(phdrs[0]), sizeof(Phdr))));
};

TEST(ElfldltlPhdrTests, ReadPhdrsFromFilePhXNum) { TestAllFormats(ReadPhdrsFromFilePhXNumTest); }

}  // namespace
