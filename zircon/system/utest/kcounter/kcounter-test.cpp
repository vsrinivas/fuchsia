// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fbl/array.h>
#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <getopt.h>
#include <lib/counter-vmo-abi.h>
#include <lib/fdio/io.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <unistd.h>
#include <utility>
#include <zircon/status.h>

#include <unittest/unittest.h>

namespace {

constexpr char kVmoFilePrefix[] = "/boot/kernel/";

bool test_counters() {
    BEGIN_TEST;

    fzl::OwnedVmoMapper desc_mapper;
    const counters::DescriptorVmo* desc;
    {
        char desc_file_name[sizeof(kVmoFilePrefix) +
                            sizeof(counters::DescriptorVmo::kVmoName)];
        strcpy(desc_file_name, kVmoFilePrefix);
        strcat(desc_file_name, counters::DescriptorVmo::kVmoName);
        fbl::unique_fd desc_fd(open(desc_file_name, O_RDONLY));
        ASSERT_TRUE(desc_fd, "cannot open descriptor VMO file");
        zx::vmo vmo;
        zx_status_t status = fdio_get_vmo_exact(
            desc_fd.get(), vmo.reset_and_get_address());
        ASSERT_EQ(status, ZX_OK, "fdio_get_vmo_exact on descriptor VMO");
        uint64_t size;
        status = vmo.get_size(&size);
        ASSERT_EQ(status, ZX_OK, "cannot get descriptor VMO size");
        status = desc_mapper.Map(std::move(vmo), size, ZX_VM_PERM_READ);
        ASSERT_EQ(status, ZX_OK, "cannot map descriptor VMO");
        desc = reinterpret_cast<counters::DescriptorVmo*>(desc_mapper.start());
        EXPECT_EQ(desc->magic, counters::DescriptorVmo::kMagic,
                  "descriptor VMO magic number");
        EXPECT_GE(size, sizeof(*desc) + desc->descriptor_table_size,
                  "descriptor table size");
    }

    fzl::OwnedVmoMapper arena_mapper;
    const volatile int64_t* arena;
    {
        char arena_file_name[sizeof(kVmoFilePrefix) +
                             sizeof(counters::kArenaVmoName)];
        strcpy(arena_file_name, kVmoFilePrefix);
        strcat(arena_file_name, counters::kArenaVmoName);
        fbl::unique_fd arena_fd(open(arena_file_name, O_RDONLY));
        ASSERT_TRUE(arena_fd, "cannot open arena VMO file");
        zx::vmo vmo;
        zx_status_t status = fdio_get_vmo_exact(
            arena_fd.get(), vmo.reset_and_get_address());
        ASSERT_EQ(status, ZX_OK, "fdio_get_vmo_exact on arena VMO");
        uint64_t size;
        status = vmo.get_size(&size);
        ASSERT_EQ(status, ZX_OK, "cannot get arena VMO size");
        EXPECT_GE(size,
                  desc->max_cpus * desc->num_counters() * sizeof(int64_t),
                  "arena VMO size");
        status = arena_mapper.Map(std::move(vmo), size, ZX_VM_PERM_READ);
        ASSERT_EQ(status, ZX_OK, "cannot map arena VMO");
        arena = reinterpret_cast<int64_t*>(arena_mapper.start());
    }

    auto find = [desc](const counters::Descriptor& ref) -> const counters::Descriptor* {
        auto result = std::equal_range(desc->begin(), desc->end(),
                                       ref, [](const counters::Descriptor& a,
                                               const counters::Descriptor& b) {
                                           return strcmp(a.name, b.name) < 0;
                                       });
        return (result.first == result.second) ?
            nullptr : &desc->descriptor_table[result.first - desc->begin()];
    };

    constexpr counters::Descriptor kExpected[] = {
        {"counters.magic", counters::Type::kSum},
        {"handles.duped", counters::Type::kSum},
        {"handles.live", counters::Type::kSum},
        {"handles.made", counters::Type::kSum},
    };
    for (const auto& ref : kExpected) {
        auto found = find(ref);
        EXPECT_NONNULL(found, "expected counter name not found");
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
                case counters::Type::kMax:  // Not used, see ZX-3337.
                    value = std::max(value, cpu_value);
                    break;
                }
            }
            EXPECT_GT(value, 0, ref.name);
            if (!strcmp(ref.name, "counters.magic")) {
                EXPECT_EQ(value, counters::DescriptorVmo::kMagic,
                          "counters.magic");
            }
        }
    }

    END_TEST;
}

}  // anonymous namespace

BEGIN_TEST_CASE(counters_test)
RUN_TEST(test_counters)
END_TEST_CASE(counters_test)
