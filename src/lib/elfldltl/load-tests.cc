// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/container.h>
#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/load.h>
#include <lib/elfldltl/memory.h>
#include <lib/elfldltl/phdr.h>
#include <lib/elfldltl/static-vector.h>
#include <lib/stdcompat/span.h>

#include <zxtest/zxtest.h>

#include "tests.h"

namespace {

constexpr size_t kPageSize = 0x1000;

constexpr auto FailToAdd = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  ExpectedSingleError error("too many PT_LOAD segments", ": maximum 0");

  elfldltl::LoadInfo<Elf, elfldltl::StaticVector<0>::Container> loadInfo;

  Phdr phdr{.memsz = 1};
  EXPECT_FALSE(loadInfo.AddSegment(error.diag(), kPageSize, phdr));
};

TEST(ElfldltlLoadTests, FailToAdd) { TestAllFormats(FailToAdd); }

constexpr auto AddEmptyPhdr = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  auto diag = ExpectOkDiagnostics();

  elfldltl::LoadInfo<Elf, elfldltl::StaticVector<0>::Container> loadInfo;

  Phdr phdr{};
  EXPECT_TRUE(loadInfo.AddSegment(diag, kPageSize, phdr));
};

TEST(ElfldltlLoadTests, EmptyPhdr) { TestAllFormats(AddEmptyPhdr); }

constexpr auto CreateConstantSegment = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  auto diag = ExpectOkDiagnostics();

  elfldltl::LoadInfo<Elf, elfldltl::StaticVector<1>::Container> loadInfo;
  using ConstantSegment = typename decltype(loadInfo)::ConstantSegment;

  Phdr phdr{.memsz = kPageSize * 10};
  EXPECT_TRUE(loadInfo.AddSegment(diag, kPageSize, phdr));

  const auto& segments = loadInfo.segments();
  ASSERT_EQ(segments.size(), 1);
  const auto& variant = segments[0];
  ASSERT_TRUE(std::holds_alternative<ConstantSegment>(variant));
  EXPECT_EQ(std::get<ConstantSegment>(variant).memsz(), phdr.memsz);
};

TEST(ElfldltlLoadTests, CreateConstantSegment) { TestAllFormats(CreateConstantSegment); }

constexpr auto CreateZeroFillSegment = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  auto diag = ExpectOkDiagnostics();

  elfldltl::LoadInfo<Elf, elfldltl::StaticVector<1>::Container> loadInfo;
  using ZeroFillSegment = typename decltype(loadInfo)::ZeroFillSegment;

  Phdr phdr{.memsz = kPageSize * 5};
  phdr.flags = Phdr::kRead | Phdr::kWrite;
  EXPECT_TRUE(loadInfo.AddSegment(diag, kPageSize, phdr));

  const auto& segments = loadInfo.segments();
  ASSERT_EQ(segments.size(), 1);
  const auto& variant = segments[0];
  ASSERT_TRUE(std::holds_alternative<ZeroFillSegment>(variant));
  EXPECT_EQ(std::get<ZeroFillSegment>(variant).memsz(), phdr.memsz);
};

TEST(ElfldltlLoadTests, CreateZeroFillSegment) { TestAllFormats(CreateZeroFillSegment); }

constexpr auto CreateDataWithZeroFillSegment = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  auto diag = ExpectOkDiagnostics();

  elfldltl::LoadInfo<Elf, elfldltl::StaticVector<1>::Container> loadInfo;
  using DataWithZeroFillSegment = typename decltype(loadInfo)::DataWithZeroFillSegment;

  Phdr phdr{.filesz = kPageSize, .memsz = kPageSize * 5};
  phdr.flags = Phdr::kRead | Phdr::kWrite;
  EXPECT_TRUE(loadInfo.AddSegment(diag, kPageSize, phdr));

  const auto& segments = loadInfo.segments();
  ASSERT_EQ(segments.size(), 1);
  const auto& variant = segments[0];
  ASSERT_TRUE(std::holds_alternative<DataWithZeroFillSegment>(variant));
  EXPECT_EQ(std::get<DataWithZeroFillSegment>(variant).memsz(), phdr.memsz());
};

TEST(ElfldltlLoadTests, CreateDataWithZeroFillSegment) {
  TestAllFormats(CreateDataWithZeroFillSegment);
}

constexpr auto CreateDataSegment = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  auto diag = ExpectOkDiagnostics();

  elfldltl::LoadInfo<Elf, elfldltl::StaticVector<1>::Container> loadInfo;
  using DataSegment = typename decltype(loadInfo)::DataSegment;

  Phdr phdr{.filesz = kPageSize, .memsz = kPageSize};
  phdr.flags = Phdr::kRead | Phdr::kWrite;
  EXPECT_TRUE(loadInfo.AddSegment(diag, kPageSize, phdr));

  const auto& segments = loadInfo.segments();
  ASSERT_EQ(segments.size(), 1);
  const auto& variant = segments[0];
  ASSERT_TRUE(std::holds_alternative<DataSegment>(variant));
  EXPECT_EQ(std::get<DataSegment>(variant).memsz(), phdr.memsz());
};

TEST(ElfldltlLoadTests, CreateDataSegment) { TestAllFormats(CreateDataSegment); }

template <bool Merged, template <typename Elf> typename Segment1,
          template <typename Elf> typename Segment2, template <typename Elf> typename GetPhdr1,
          template <typename Elf> typename GetPhdr2>
constexpr auto CreateMergeTest() {
  return [](auto&& elf) {
    using Elf = std::decay_t<decltype(elf)>;
    using Segment1T = Segment1<Elf>;
    using Segment2T = Segment2<Elf>;
    constexpr int totalSegments = Merged ? 1 : 2;

    auto diag = ExpectOkDiagnostics();

    elfldltl::LoadInfo<Elf, elfldltl::StaticVector<2>::Container> loadInfo;
    const auto& segments = loadInfo.segments();

    int offset = 0;
    auto phdr1 = GetPhdr1<Elf>{}(offset);
    auto phdr2 = GetPhdr2<Elf>{}(offset);
    auto expectedSize = Merged ? phdr1.memsz() + phdr2.memsz() : phdr2.memsz();

    loadInfo.AddSegment(diag, kPageSize, phdr1);
    ASSERT_EQ(segments.size(), 1);
    ASSERT_TRUE(std::holds_alternative<Segment1T>(segments.back()));
    EXPECT_EQ(std::get<Segment1T>(segments.back()).memsz(), phdr1.memsz());
    loadInfo.AddSegment(diag, kPageSize, phdr2);
    ASSERT_EQ(segments.size(), totalSegments);
    ASSERT_TRUE(std::holds_alternative<Segment2T>(segments.back()));
    EXPECT_EQ(std::get<Segment2T>(segments.back()).memsz(), expectedSize);
  };
}

template <template <typename Elf> typename Segment1, template <typename Elf> typename Segment2,
          template <typename Elf> typename GetPhdr1, template <typename Elf> typename GetPhdr2>
constexpr auto GetMergeTest() {
  return CreateMergeTest<true, Segment1, Segment2, GetPhdr1, GetPhdr2>();
}

template <template <typename Elf> typename Segment1, template <typename Elf> typename Segment2,
          template <typename Elf> typename GetPhdr1, template <typename Elf> typename GetPhdr2>
constexpr auto GetNotMergedTest() {
  return CreateMergeTest<false, Segment1, Segment2, GetPhdr1, GetPhdr2>();
}

template <template <typename Elf> typename Segment, template <typename Elf> typename GetPhdr>
constexpr auto GetMergeSameTest() {
  return GetMergeTest<Segment, Segment, GetPhdr, GetPhdr>();
}

template <uint64_t Flags, uint64_t FileSz = kPageSize, uint64_t MemSz = kPageSize>
struct CreatePhdr {
  template <typename Elf>
  struct type {
    auto operator()(int& offset) {
      using Phdr = typename Elf::Phdr;
      Phdr phdr{.type = elfldltl::ElfPhdrType::kLoad,
                .offset = offset,
                .vaddr = offset,
                .filesz = FileSz,
                .memsz = MemSz};
      phdr.flags = Flags;
      offset += kPageSize;
      return phdr;
    }
  };
};

template <typename Elf>
using ConstantSegment =
    typename elfldltl::LoadInfo<Elf, elfldltl::StaticVector<0>::Container>::ConstantSegment;

template <typename Elf>
using ConstantPhdr = CreatePhdr<elfldltl::PhdrBase::kRead>::type<Elf>;

template <typename Elf>
using ZeroFillSegment =
    typename elfldltl::LoadInfo<Elf, elfldltl::StaticVector<0>::Container>::ZeroFillSegment;

template <typename Elf>
using ZeroFillPhdr =
    CreatePhdr<elfldltl::PhdrBase::kRead | elfldltl::PhdrBase::kWrite, 0>::type<Elf>;

template <typename Elf>
using DataWithZeroFillSegment =
    typename elfldltl::LoadInfo<Elf, elfldltl::StaticVector<0>::Container>::DataWithZeroFillSegment;

template <typename Elf>
using DataWithZeroFillPhdr = CreatePhdr<elfldltl::PhdrBase::kRead | elfldltl::PhdrBase::kWrite,
                                        kPageSize, kPageSize * 2>::type<Elf>;

template <typename Elf>
using DataSegment =
    typename elfldltl::LoadInfo<Elf, elfldltl::StaticVector<0>::Container>::DataSegment;

template <typename Elf>
using DataPhdr = CreatePhdr<elfldltl::PhdrBase::kRead | elfldltl::PhdrBase::kWrite>::type<Elf>;

TEST(ElfldltlLoadTests, MergeSameConstantSegment) {
  TestAllFormats(GetMergeSameTest<ConstantSegment, ConstantPhdr>());
}

TEST(ElfldltlLoadTests, MergeSameDataSegment) {
  TestAllFormats(GetMergeSameTest<DataSegment, DataPhdr>());
}

TEST(ElfldltlLoadTests, MergeDataAndZeroFill) {
  TestAllFormats(GetMergeTest<DataSegment, DataWithZeroFillSegment, DataPhdr, ZeroFillPhdr>());
}

TEST(ElfldltlLoadTests, MergeDataAndDataWithZeroFill) {
  TestAllFormats(
      GetMergeTest<DataSegment, DataWithZeroFillSegment, DataPhdr, DataWithZeroFillPhdr>());
}

TEST(ElfldltlLoadTests, CantMergeConstant) {
  TestAllFormats(GetNotMergedTest<ConstantSegment, ZeroFillSegment, ConstantPhdr, ZeroFillPhdr>());
  TestAllFormats(GetNotMergedTest<ConstantSegment, DataWithZeroFillSegment, ConstantPhdr,
                                  DataWithZeroFillPhdr>());
  TestAllFormats(GetNotMergedTest<ConstantSegment, DataSegment, ConstantPhdr, DataPhdr>());
}

TEST(ElfldltlLoadTests, CantMergeZeroFill) {
  TestAllFormats(GetNotMergedTest<ZeroFillSegment, ConstantSegment, ZeroFillPhdr, ConstantPhdr>());
  // Logically two ZeroFillSegment's could be merged but we don't currently do this because these
  // are unlikely to exist in the wild.
  TestAllFormats(GetNotMergedTest<ZeroFillSegment, ZeroFillSegment, ZeroFillPhdr, ZeroFillPhdr>());
  TestAllFormats(GetNotMergedTest<ZeroFillSegment, DataWithZeroFillSegment, ZeroFillPhdr,
                                  DataWithZeroFillPhdr>());
  TestAllFormats(GetNotMergedTest<ZeroFillSegment, DataSegment, ZeroFillPhdr, DataPhdr>());
}

TEST(ElfldltlLoadTests, CantMergeDataAndZeroFill) {
  TestAllFormats(GetNotMergedTest<DataWithZeroFillSegment, ConstantSegment, DataWithZeroFillPhdr,
                                  ConstantPhdr>());
  TestAllFormats(GetNotMergedTest<DataWithZeroFillSegment, DataWithZeroFillSegment,
                                  DataWithZeroFillPhdr, DataWithZeroFillPhdr>());
  TestAllFormats(
      GetNotMergedTest<DataWithZeroFillSegment, DataSegment, DataWithZeroFillPhdr, DataPhdr>());
}

TEST(ElfldltlLoadTests, CantMergeData) {
  TestAllFormats(GetNotMergedTest<DataSegment, ConstantSegment, DataPhdr, ConstantPhdr>());
}

constexpr auto GetPhdrObserver = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  auto diag = ExpectOkDiagnostics();

  elfldltl::LoadInfo<Elf, elfldltl::StdContainer<std::vector>::Container> loadInfo;
  using ConstantSegment = typename decltype(loadInfo)::ConstantSegment;
  using DataWithZeroFillSegment = typename decltype(loadInfo)::DataWithZeroFillSegment;

  int offset = 0;
  const Phdr kPhdrs[] = {
      ConstantPhdr<Elf>{}(offset), ConstantPhdr<Elf>{}(offset), DataPhdr<Elf>{}(offset),
      DataPhdr<Elf>{}(offset),     ZeroFillPhdr<Elf>{}(offset),
  };

  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs), loadInfo.GetPhdrObserver(kPageSize)));
  const auto& segments = loadInfo.segments();
  EXPECT_EQ(segments.size(), 2);
  ASSERT_TRUE(std::holds_alternative<ConstantSegment>(segments[0]));
  EXPECT_EQ(std::get<ConstantSegment>(segments[0]).memsz(), kPhdrs[0].memsz + kPhdrs[1].memsz);
  ASSERT_TRUE(std::holds_alternative<DataWithZeroFillSegment>(segments[1]));
  EXPECT_EQ(std::get<DataWithZeroFillSegment>(segments[1]).memsz(),
            kPhdrs[2].memsz + kPhdrs[3].memsz + kPhdrs[4].memsz);
};

TEST(ElfldltlLoadTests, GetPhdrObserver) { TestAllFormats(GetPhdrObserver); }

constexpr auto VisitSegments = [](auto&& elf) {
  using Elf = std::decay_t<decltype(elf)>;
  using Phdr = typename Elf::Phdr;

  auto diag = ExpectOkDiagnostics();

  elfldltl::LoadInfo<Elf, elfldltl::StdContainer<std::vector>::Container> loadInfo;

  ASSERT_EQ(loadInfo.segments().size(), 0);
  EXPECT_TRUE(loadInfo.VisitSegments([](auto&& segment) {
    ADD_FAILURE();
    return true;
  }));

  int offset = 0;
  const Phdr kPhdrs[] = {
      ConstantPhdr<Elf>{}(offset),
      DataPhdr<Elf>{}(offset),
  };

  EXPECT_TRUE(
      elfldltl::DecodePhdrs(diag, cpp20::span(kPhdrs), loadInfo.GetPhdrObserver(kPageSize)));
  ASSERT_EQ(loadInfo.segments().size(), 2);

  int currentIndex = 0;
  EXPECT_TRUE(loadInfo.VisitSegments([&](auto&& segment) {
    EXPECT_EQ(segment.offset(), kPhdrs[currentIndex++].offset);
    return true;
  }));

  currentIndex = 0;
  EXPECT_FALSE(loadInfo.VisitSegments([&](auto&& segment) {
    EXPECT_EQ(currentIndex++, 0);
    return false;
  }));
};

TEST(ElfldltlLoadTests, VisitSegments) { TestAllFormats(VisitSegments); }

}  // namespace
