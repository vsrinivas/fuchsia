// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/mapped-vmo.h>

#include <magenta/syscalls.h>

#include <unittest/unittest.h>

bool test_mapped_vmo() {
    BEGIN_TEST;

    size_t init_size = 512 * (1 << 10);
    size_t min_size = 256 * (1 << 10);
    size_t max_size = 1 << 20;

    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[max_size]);
    ASSERT_TRUE(ac.check());

    unsigned seed = static_cast<unsigned>(mx_ticks_get());
    srand(seed);
    for (unsigned n = 0; n < max_size; n++) {
        buf[n] = static_cast<uint8_t>(rand_r(&seed));
    }

    // Create MappedVmo
    fbl::unique_ptr<MappedVmo> mvmo;
    ASSERT_EQ(MappedVmo::Create(init_size, "test-vmo", &mvmo), MX_OK);

    // Verify size & data
    ASSERT_EQ(mvmo->GetSize(), init_size);
    memcpy(mvmo->GetData(), buf.get(), init_size);
    ASSERT_EQ(memcmp(buf.get(), mvmo->GetData(), init_size), 0);

    // Grow vmo with size not divisable by page size, check size
    ASSERT_EQ(mvmo->Grow(init_size + 1), MX_OK);
    ASSERT_EQ(mvmo->GetSize(), init_size + PAGE_SIZE);

    // Shrink vmo, verify size & data
    ASSERT_EQ(mvmo->Shrink(0, min_size), MX_OK);
    ASSERT_EQ(mvmo->GetSize(), min_size);
    ASSERT_EQ(memcmp(buf.get(), mvmo->GetData(), min_size), 0);

    // Grow vmo, verify size & data
    ASSERT_EQ(mvmo->Grow(max_size), MX_OK);
    ASSERT_EQ(mvmo->GetSize(), max_size);
    ASSERT_EQ(memcmp(buf.get(), mvmo->GetData(), min_size), 0);
    uintptr_t addr = reinterpret_cast<uintptr_t>(mvmo->GetData());
    memcpy(reinterpret_cast<void*>(addr + min_size), reinterpret_cast<void*>(buf.get() + min_size), max_size - min_size);
    ASSERT_EQ(memcmp(buf.get(), mvmo->GetData(), max_size), 0);

    END_TEST;
}

BEGIN_TEST_CASE(util_tests)
RUN_TEST_MEDIUM(test_mapped_vmo)
END_TEST_CASE(util_tests)
