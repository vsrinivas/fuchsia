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

constexpr size_t kAlign = 0x1000;  // Example alignment.

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

}  // namespace
