// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <lib/counter-vmo-abi.h>
#include <lib/fdio/io.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <unistd.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <fbl/array.h>
#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "src/zircon/bin/kcounter/kcounter_cmdline.h"

namespace {

constexpr char kVmoFilePrefix[] = "/boot/kernel/";

TEST(Counters, Basic) {
  fzl::OwnedVmoMapper desc_mapper;
  const counters::DescriptorVmo* desc;
  {
    char desc_file_name[sizeof(kVmoFilePrefix) + sizeof(counters::DescriptorVmo::kVmoName)];
    strcpy(desc_file_name, kVmoFilePrefix);
    strcat(desc_file_name, counters::DescriptorVmo::kVmoName);
    fbl::unique_fd desc_fd(open(desc_file_name, O_RDONLY));
    ASSERT_TRUE(desc_fd, "cannot open descriptor VMO file");
    zx::vmo vmo;
    ASSERT_OK(fdio_get_vmo_exact(desc_fd.get(), vmo.reset_and_get_address()));
    uint64_t size;
    ASSERT_OK(vmo.get_size(&size));
    ASSERT_OK(desc_mapper.Map(std::move(vmo), size, ZX_VM_PERM_READ));
    desc = reinterpret_cast<counters::DescriptorVmo*>(desc_mapper.start());
    EXPECT_EQ(desc->magic, counters::DescriptorVmo::kMagic, "descriptor VMO magic number");
    EXPECT_GE(size, sizeof(*desc) + desc->descriptor_table_size, "descriptor table size");
  }

  fzl::OwnedVmoMapper arena_mapper;
  const volatile int64_t* arena;
  {
    char arena_file_name[sizeof(kVmoFilePrefix) + sizeof(counters::kArenaVmoName)];
    strcpy(arena_file_name, kVmoFilePrefix);
    strcat(arena_file_name, counters::kArenaVmoName);
    fbl::unique_fd arena_fd(open(arena_file_name, O_RDONLY));
    ASSERT_TRUE(arena_fd, "cannot open arena VMO file");
    zx::vmo vmo;
    ASSERT_OK(fdio_get_vmo_exact(arena_fd.get(), vmo.reset_and_get_address()));
    uint64_t size;
    ASSERT_OK(vmo.get_size(&size));
    EXPECT_GE(size, desc->max_cpus * desc->num_counters() * sizeof(int64_t), "arena VMO size");
    ASSERT_OK(arena_mapper.Map(std::move(vmo), size, ZX_VM_PERM_READ));
    arena = reinterpret_cast<int64_t*>(arena_mapper.start());
  }

  auto find = [desc](const counters::Descriptor& ref) -> const counters::Descriptor* {
    auto result =
        std::equal_range(desc->begin(), desc->end(), ref,
                         [](const counters::Descriptor& a, const counters::Descriptor& b) {
                           return strcmp(a.name, b.name) < 0;
                         });
    return (result.first == result.second) ? nullptr
                                           : &desc->descriptor_table[result.first - desc->begin()];
  };

  constexpr counters::Descriptor kExpected[] = {
      {"init.target.time.msec", counters::Type::kSum},
      {"handles.duped", counters::Type::kSum},
      {"handles.live", counters::Type::kSum},
      {"handles.made", counters::Type::kSum},
  };
  for (const auto& ref : kExpected) {
    auto found = find(ref);
    EXPECT_NOT_NULL(found, "expected counter name not found");
    if (found) {
      EXPECT_EQ(found->type, ref.type, "counter has wrong type");
      size_t idx = found - desc->begin();
      int64_t value = 0;
      for (uint64_t cpu = 0; cpu < desc->max_cpus; ++cpu) {
        int64_t cpu_value = arena[(cpu * desc->num_counters()) + idx];
        switch (ref.type) {
          default:
            abort();
            break;
          case counters::Type::kSum:
            value += cpu_value;
            break;
          case counters::Type::kMax:  // Not used, see fxbug.dev/33140.
            value = std::max(value, cpu_value);
            break;
        }
      }
      EXPECT_GT(value, 0);
    }
  }
}

TEST(Counters, CmdlineNormalSuccess) {
  const char* const argv[] = {"self.exe", "-v", "-w", "channel", nullptr};

  KcounterCmdline cmdline;
  ASSERT_TRUE(
      kcounter_parse_cmdline(static_cast<int>(countof(argv)), argv, /*err=*/nullptr, &cmdline));
  EXPECT_FALSE(cmdline.help);
  EXPECT_FALSE(cmdline.list);
  EXPECT_FALSE(cmdline.terse);
  EXPECT_TRUE(cmdline.verbose);
  EXPECT_GT(cmdline.period, 1);
  EXPECT_EQ(cmdline.unparsed_args_start, 3);
}

TEST(Counters, CmdlineFailListAndTerse) {
  const char* const argv[] = {"self.exe", "-l", "-t", nullptr};

  KcounterCmdline cmdline;
  char errbuf[2048];
  FILE* err = fmemopen(errbuf, sizeof(errbuf), "w");
  ASSERT_TRUE(err);
  ASSERT_FALSE(kcounter_parse_cmdline(static_cast<int>(countof(argv)), argv, err, &cmdline));
  fclose(err);
  ASSERT_TRUE(strstr(errbuf, "--list, --terse"));
  ASSERT_TRUE(strstr(errbuf, "Usage: self.exe"));
}

TEST(Counters, CmdlineFailTerseAndVerbose) {
  const char* const argv[] = {"self.exe", "--terse", "-v", nullptr};

  KcounterCmdline cmdline;
  char errbuf[2048];
  FILE* err = fmemopen(errbuf, sizeof(errbuf), "w");
  ASSERT_TRUE(err);
  ASSERT_FALSE(kcounter_parse_cmdline(static_cast<int>(countof(argv)), argv, err, &cmdline));
  fclose(err);
  ASSERT_TRUE(strstr(errbuf, "--terse, and --verbose are mutually exclusive"));
}

TEST(Counters, CmdlineFailListAndWatch) {
  const char* const argv[] = {"self.exe", "-l", "-w", "things", nullptr};

  KcounterCmdline cmdline;
  char errbuf[2048];
  FILE* err = fmemopen(errbuf, sizeof(errbuf), "w");
  ASSERT_TRUE(err);
  ASSERT_FALSE(kcounter_parse_cmdline(static_cast<int>(countof(argv)), argv, err, &cmdline));
  fclose(err);
  ASSERT_TRUE(strstr(errbuf, "--list and --watch are mutually exclusive"));
}

}  // anonymous namespace
