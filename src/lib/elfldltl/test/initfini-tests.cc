// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/dynamic.h>
#include <lib/elfldltl/init-fini.h>
#include <lib/elfldltl/memory.h>

#include <array>
#include <string>
#include <vector>

#include <zxtest/zxtest.h>

#include "tests.h"

namespace {
using NativeInfo = elfldltl::InitFiniInfo<elfldltl::Elf<>>;

template <class Elf>
constexpr typename Elf::size_type kImageAddr = 0x1234000;

template <class Elf>
constexpr typename Elf::Addr kImageData[] = {1, 2, 3, 4};

template <class Elf>
constexpr cpp20::span kImage(kImageData<Elf>);

template <class Elf>
const auto kImageBytes = cpp20::as_bytes(kImage<Elf>);

template <typename Dyn, size_t N>
constexpr cpp20::span<const Dyn> DynSpan(const std::array<Dyn, N>& dyn) {
  return {dyn};
}

constexpr elfldltl::DiagnosticsFlags kDiagFlags = {.multiple_errors = true};

constexpr auto EmptyTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Dyn = typename Elf::Dyn;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kDiagFlags);
  elfldltl::DirectMemory memory{
      {
          const_cast<std::byte*>(kImageBytes<Elf>.data()),
          kImageBytes<Elf>.size(),
      },
      kImageAddr<Elf>,
  };

  constexpr std::array dyn{
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::InitFiniInfo<Elf> info;
  EXPECT_TRUE(
      elfldltl::DecodeDynamic(diag, memory, DynSpan(dyn), elfldltl::DynamicInitObserver(info)));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  EXPECT_EQ(0, errors.size());

  EXPECT_EQ(0, info.size());
  info.VisitInit([](auto&&... args) { FAIL("should not be called"); }, 0);
  info.VisitFini([](auto&&... args) { FAIL("should not be called"); }, 0);
};

TEST(ElfldltlInitFiniTests, Empty) { TestAllFormats(EmptyTest); }

constexpr auto ArrayOnlyTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Dyn = typename Elf::Dyn;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kDiagFlags);
  elfldltl::DirectMemory memory{
      {
          const_cast<std::byte*>(kImageBytes<Elf>.data()),
          kImageBytes<Elf>.size(),
      },
      kImageAddr<Elf>,
  };

  constexpr std::array dyn{
      Dyn{.tag = elfldltl::ElfDynTag::kInitArray, .val = kImageAddr<Elf>},
      Dyn{.tag = elfldltl::ElfDynTag::kInitArraySz, .val = kImageBytes<Elf>.size()},
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::InitFiniInfo<Elf> info;
  EXPECT_TRUE(
      elfldltl::DecodeDynamic(diag, memory, DynSpan(dyn), elfldltl::DynamicInitObserver(info)));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  EXPECT_EQ(0, errors.size());

  EXPECT_EQ(4, info.size());
};

TEST(ElfldltlInitFiniTests, ArrayOnly) { TestAllFormats(ArrayOnlyTest); }

constexpr auto LegacyOnlyTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Dyn = typename Elf::Dyn;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kDiagFlags);
  elfldltl::DirectMemory memory{
      {
          const_cast<std::byte*>(kImageBytes<Elf>.data()),
          kImageBytes<Elf>.size(),
      },
      kImageAddr<Elf>,
  };

  constexpr std::array dyn{
      Dyn{.tag = elfldltl::ElfDynTag::kInit, .val = 0x5678},
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::InitFiniInfo<Elf> info;
  EXPECT_TRUE(
      elfldltl::DecodeDynamic(diag, memory, DynSpan(dyn), elfldltl::DynamicInitObserver(info)));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  EXPECT_EQ(0, errors.size());

  EXPECT_EQ(1, info.size());
  EXPECT_EQ(0x5678, info.legacy());
};

TEST(ElfldltlInitFiniTests, LegacyOnly) { TestAllFormats(LegacyOnlyTest); }

constexpr auto ArrayWithLegacyTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Dyn = typename Elf::Dyn;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kDiagFlags);
  elfldltl::DirectMemory memory{
      {
          const_cast<std::byte*>(kImageBytes<Elf>.data()),
          kImageBytes<Elf>.size(),
      },
      kImageAddr<Elf>,
  };

  constexpr std::array dyn{
      Dyn{.tag = elfldltl::ElfDynTag::kInit, .val = 0x5678},
      Dyn{.tag = elfldltl::ElfDynTag::kInitArray, .val = kImageAddr<Elf>},
      Dyn{.tag = elfldltl::ElfDynTag::kInitArraySz, .val = kImageBytes<Elf>.size()},
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::InitFiniInfo<Elf> info;
  EXPECT_TRUE(
      elfldltl::DecodeDynamic(diag, memory, DynSpan(dyn), elfldltl::DynamicInitObserver(info)));

  EXPECT_EQ(0, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  EXPECT_EQ(0, errors.size());

  EXPECT_EQ(5, info.size());
};

TEST(ElfldltlInitFiniTests, ArrayWithLegacy) { TestAllFormats(ArrayWithLegacyTest); }

constexpr auto MissingArrayTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Dyn = typename Elf::Dyn;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kDiagFlags);
  elfldltl::DirectMemory memory{
      {
          const_cast<std::byte*>(kImageBytes<Elf>.data()),
          kImageBytes<Elf>.size(),
      },
      kImageAddr<Elf>,
  };

  constexpr std::array dyn{
      // DT_INIT_ARRAY missing with DT_INIT_ARRAYSZ present.
      Dyn{.tag = elfldltl::ElfDynTag::kInitArraySz, .val = kImageBytes<Elf>.size()},
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::InitFiniInfo<Elf> info;
  EXPECT_TRUE(
      elfldltl::DecodeDynamic(diag, memory, DynSpan(dyn), elfldltl::DynamicInitObserver(info)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  EXPECT_EQ(1, errors.size());

  EXPECT_EQ(0, info.size());
};

TEST(ElfldltlInitFiniTests, MissingArray) { TestAllFormats(MissingArrayTest); }

constexpr auto MissingSizeTest = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Dyn = typename Elf::Dyn;

  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors, kDiagFlags);
  elfldltl::DirectMemory memory{
      {
          const_cast<std::byte*>(kImageBytes<Elf>.data()),
          kImageBytes<Elf>.size(),
      },
      kImageAddr<Elf>,
  };

  constexpr std::array dyn{
      Dyn{.tag = elfldltl::ElfDynTag::kInitArray, .val = kImageAddr<Elf>},
      // DT_INIT_ARRAYSZ missing with DT_INIT_ARRAY present.
      Dyn{.tag = elfldltl::ElfDynTag::kNull},
  };

  elfldltl::InitFiniInfo<Elf> info;
  EXPECT_TRUE(
      elfldltl::DecodeDynamic(diag, memory, DynSpan(dyn), elfldltl::DynamicInitObserver(info)));

  EXPECT_EQ(1, diag.errors());
  EXPECT_EQ(0, diag.warnings());
  EXPECT_EQ(1, errors.size());

  EXPECT_EQ(0, info.size());
};

TEST(ElfldltlInitFiniTests, MissingSize) { TestAllFormats(MissingSizeTest); }

constexpr auto VisitInitTests = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;

  constexpr typename Elf::Addr array[] = {2, 3, 4, 5};
  elfldltl::InitFiniInfo<Elf> info;
  info.set_array(cpp20::span(array));
  info.set_legacy(1);

  ASSERT_EQ(5, info.size());

  info.VisitInit(
      [i = size_type{1}](size_type addr, bool relocated) mutable {
        EXPECT_EQ(i, addr);
        EXPECT_EQ(relocated, addr != 1);
        ++i;
      },
      true);

  info.VisitInit(
      [i = size_type{1}](size_type addr, bool relocated) mutable {
        EXPECT_EQ(i, addr);
        EXPECT_FALSE(relocated);
        ++i;
      },
      false);
};

TEST(ElfldltlInitFiniTests, VisitInit) { TestAllFormats(VisitInitTests); }

constexpr auto VisitFiniTests = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using size_type = typename Elf::size_type;

  constexpr typename Elf::Addr array[] = {2, 3, 4, 5};
  elfldltl::InitFiniInfo<Elf> info;
  info.set_array(cpp20::span(array));
  info.set_legacy(1);

  ASSERT_EQ(5, info.size());

  info.VisitFini(
      [i = size_type{5}](size_type addr, bool relocated) mutable {
        EXPECT_EQ(i, addr);
        EXPECT_EQ(relocated, addr != 1);
        --i;
      },
      true);

  info.VisitFini(
      [i = size_type{5}](size_type addr, bool relocated) mutable {
        EXPECT_EQ(i, addr);
        EXPECT_FALSE(relocated);
        --i;
      },
      false);
};

TEST(ElfldltlInitFiniTests, VisitFini) { TestAllFormats(VisitFiniTests); }

// The tests for CallInit and CallFini must use global state since
// the callees are simple function pointers taking no arguments.
std::vector<int> gCalls;

template <int I>
void AppendCall() {
  gCalls.push_back(I);
}

const std::array<elfldltl::Elf<>::Addr, 3> gThreeCalls = {
    reinterpret_cast<uintptr_t>(&AppendCall<1>),
    reinterpret_cast<uintptr_t>(&AppendCall<2>),
    reinterpret_cast<uintptr_t>(&AppendCall<3>),
};

TEST(ElfldltlInitFiniTests, CallInitNoLegacy) {
  NativeInfo info;
  info.set_array(gThreeCalls);

  gCalls.clear();
  info.CallInit(0);

  ASSERT_EQ(gCalls.size(), 3u);
  EXPECT_EQ(gCalls[0], 1);
  EXPECT_EQ(gCalls[1], 2);
  EXPECT_EQ(gCalls[2], 3);
}

TEST(ElfldltlInitFiniTests, CallInitWithLegacy) {
  NativeInfo info;
  info.set_array(gThreeCalls);

  constexpr auto kRelocationAdjustment = kImageAddr<elfldltl::Elf<>>;

  info.set_legacy(reinterpret_cast<uintptr_t>(&AppendCall<0>) - kRelocationAdjustment);

  gCalls.clear();
  info.CallInit(kRelocationAdjustment);

  ASSERT_EQ(gCalls.size(), 4u);
  EXPECT_EQ(gCalls[0], 0);
  EXPECT_EQ(gCalls[1], 1);
  EXPECT_EQ(gCalls[2], 2);
  EXPECT_EQ(gCalls[3], 3);
}

TEST(ElfldltlInitFiniTests, CallFiniNoLegacy) {
  NativeInfo info;
  info.set_array(gThreeCalls);

  gCalls.clear();
  info.CallFini(0);

  ASSERT_EQ(gCalls.size(), 3u);
  EXPECT_EQ(gCalls[0], 3);
  EXPECT_EQ(gCalls[1], 2);
  EXPECT_EQ(gCalls[2], 1);
}

TEST(ElfldltlInitFiniTests, CallFiniWithLegacy) {
  NativeInfo info;
  info.set_array(gThreeCalls);

  constexpr auto kRelocationAdjustment = kImageAddr<elfldltl::Elf<>>;

  info.set_legacy(reinterpret_cast<uintptr_t>(&AppendCall<0>) - kRelocationAdjustment);

  gCalls.clear();
  info.CallFini(kRelocationAdjustment);

  ASSERT_EQ(gCalls.size(), 4u);
  EXPECT_EQ(gCalls[0], 3);
  EXPECT_EQ(gCalls[1], 2);
  EXPECT_EQ(gCalls[2], 1);
  EXPECT_EQ(gCalls[3], 0);
}

}  // namespace
