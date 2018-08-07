// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/interrupt.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

__BEGIN_CDECLS;
extern zx_handle_t get_root_resource(void);
__END_CDECLS;

static const size_t mmio_test_size = (PAGE_SIZE * 4);
static uint64_t mmio_test_base;

const zx::unowned_resource root() {
    static zx_handle_t root = get_root_resource();
    return zx::unowned_resource(root);
}

// Physical memory is reserved during boot and its location varies based on
// system and architecture. What this 'test' does is scan MMIO space looking
// for a valid region to test against, ensuring that the only errors it sees
// are 'ZX_ERR_NOT_FOUND', which indicates that it is missing from the
// region allocator.
//
// TODO(ZX-2419): Figure out a way to test IRQs in the same manner, without
// hardcoding target-specific IRQ vectors in these tests. That information is
// stored in the kernel and is not exposed to userspace, so we can't simply
// guess/probe valid vectors like we can MMIO and still assume the tests are
// valid.

static bool probe_address_space(void) {
    BEGIN_TEST;

    zx_status_t status;
    // Scan mmio in chunks until we find a gap that isn't exclusively reserved physical memory.
    uint64_t step = 0x100000000;
    for (uint64_t base = 0; base < UINT64_MAX - step; base += step) {
        zx::resource handle;
        status = zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, base,
                                      mmio_test_size, NULL, 0, &handle);
        if (status == ZX_OK) {
            mmio_test_base = base;
            break;
        }

        // If ZX_OK wasn't returned, then we should see ZX_ERR_NOT_FOUND and nothing else.
        ASSERT_EQ(ZX_ERR_NOT_FOUND, status, "");
    }

    END_TEST;
}

// This is a basic smoketest for creating resources and verifying the internals
// returned by zx_object_get_info match what the caller passed for creation.
static bool test_basic_actions(void) {
    BEGIN_TEST;

    zx::resource new_root;
    zx_info_resource_t info;
    char root_name[] = "root";

    // Create a root and verify the fields are still zero, but the name matches.
    EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_ROOT, 0, 0,
                                   root_name, sizeof(root_name), &new_root),
              ZX_OK, "");
    ASSERT_EQ(new_root.get_info(ZX_INFO_RESOURCE, &info, sizeof(info), NULL, NULL), ZX_OK, "");
    EXPECT_EQ(info.kind, ZX_RSRC_KIND_ROOT, "");
    EXPECT_EQ(info.base, 0u, "");
    EXPECT_EQ(info.size, 0u, "");
    EXPECT_EQ(info.flags, 0u, "");
    EXPECT_EQ(0, strncmp(root_name, info.name, ZX_MAX_NAME_LEN), "");

    // Check that a resource is created with all the parameters passed to the syscall, and use
    // the new root resource created for good measure.
    zx::resource mmio;
    uint32_t kind = ZX_RSRC_KIND_MMIO;
    uint32_t flags = ZX_RSRC_FLAG_EXCLUSIVE;
    char mmio_name[] = "test_resource_name";
    ASSERT_EQ(zx::resource::create(new_root, kind | flags, mmio_test_base, mmio_test_size,
                                       mmio_name, sizeof(mmio_name), &mmio),
              ZX_OK, "");
    ASSERT_EQ(mmio.get_info(ZX_INFO_RESOURCE, &info, sizeof(info), NULL, NULL), ZX_OK, "");
    EXPECT_EQ(info.kind, kind, "");
    EXPECT_EQ(info.flags, flags, "");
    EXPECT_EQ(info.base, mmio_test_base, "");
    EXPECT_EQ(info.size, mmio_test_size, "");
    EXPECT_EQ(0, strncmp(info.name, mmio_name, ZX_MAX_NAME_LEN), "");

    END_TEST;
}

// This test covers every path that returns ZX_ERR_INVALID_ARGS from the the syscall.
static bool test_invalid_args(void) {
    BEGIN_TEST;
    zx::resource temp;
    zx::resource fail_hnd;
    // test privilege inversion by seeing if an MMIO resource can other resources.
    EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, mmio_test_base,
                                   mmio_test_size, NULL, 0, &temp),
              ZX_OK, "");
    EXPECT_EQ(zx::resource::create(temp, ZX_RSRC_KIND_ROOT, 0, 0, NULL, 0, &fail_hnd),
              ZX_ERR_ACCESS_DENIED, "");
    EXPECT_EQ(zx::resource::create(temp, ZX_RSRC_KIND_MMIO, mmio_test_base, mmio_test_size, NULL,
                                   0, &fail_hnd),
              ZX_ERR_ACCESS_DENIED, "");

    // test invalid kind
    EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_COUNT, mmio_test_base,
                               mmio_test_size, NULL, 0, &temp),
              ZX_ERR_INVALID_ARGS, "");
    EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_COUNT + 1,
                                   mmio_test_base, mmio_test_size, NULL, 0, &temp),
              ZX_ERR_INVALID_ARGS, "");

    // test invalid base
    EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, UINT64_MAX, 1024,
                                   NULL, 0, &temp),
              ZX_ERR_INVALID_ARGS, "");
    // test invalid size
    EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, 1024, UINT64_MAX,
                                   NULL, 0, &temp),
              ZX_ERR_INVALID_ARGS, "");
    // test invalid options
    EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO | 0xFF0000, mmio_test_base,
                               mmio_test_size, NULL, 0, &temp),
              ZX_ERR_INVALID_ARGS, "");

    END_TEST;
}

static bool test_exclusive_shared(void) {
    // Try to create a shared  resource and ensure it blocks an exclusive
    // resource.
    BEGIN_TEST;
    zx::resource mmio_1, mmio_2;
	EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO | ZX_RSRC_FLAG_EXCLUSIVE,
                                    mmio_test_base, mmio_test_size, NULL, 0, &mmio_1),
              ZX_OK, "");
    EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, mmio_test_base,
                               mmio_test_size, NULL, 0, &mmio_2),
              ZX_ERR_NOT_FOUND, "");
    END_TEST;
}

static bool test_shared_exclusive(void) {
    // Try to create a shared  resource and ensure it blocks an exclusive
    // resource.
    BEGIN_TEST;
    zx::resource mmio_1, mmio_2;
    EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, mmio_test_base,
                               mmio_test_size, NULL, 0, &mmio_1),
              ZX_OK, "");
    EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO | ZX_RSRC_FLAG_EXCLUSIVE,
                                   mmio_test_base, mmio_test_size, NULL, 0, &mmio_2),
              ZX_ERR_NOT_FOUND, "");
    END_TEST;
}

static bool test_vmo_creation(void) {
    // Attempt to create a resource and then a vmo using that resource.
    BEGIN_TEST;
    zx::resource mmio;
    zx::vmo vmo;
    ASSERT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, mmio_test_base,
                mmio_test_size, NULL, 0, &mmio),
            ZX_OK, "");
    EXPECT_EQ(zx_vmo_create_physical(mmio.get(), mmio_test_base, PAGE_SIZE,
                                     vmo.reset_and_get_address()),
            ZX_OK, "");
    END_TEST;
}

static bool test_vmo_creation_smaller(void) {
    // Attempt to create a resource smaller than a page and ensure it still expands access to the
    // entire page.
    BEGIN_TEST;
    zx::resource mmio;
    zx::vmo vmo;
    ASSERT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, mmio_test_base,
                               PAGE_SIZE/2, NULL, 0, &mmio),
              ZX_OK, "");
    EXPECT_EQ(zx_vmo_create_physical(mmio.get(), mmio_test_base, PAGE_SIZE,
                                     vmo.reset_and_get_address()),
              ZX_OK, "");
    END_TEST;
}

static bool test_vmo_creation_unaligned(void) {
    // Attempt to create an unaligned resource and ensure that the bounds are rounded appropriately
    // to the proper PAGE_SIZE.
    BEGIN_TEST;
    zx::resource mmio;
    zx::vmo vmo;
    ASSERT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO,
                                   mmio_test_base + 0x7800, 0x2000, NULL, 0, &mmio),
              ZX_OK, "");
    EXPECT_EQ(zx_vmo_create_physical(mmio.get(), mmio_test_base + 0x7000, 0x2000,
                                     vmo.reset_and_get_address()),
              ZX_OK, "");
    END_TEST;
}

#if defined(__x86_64__)
static bool test_ioports(void) {
    BEGIN_TEST;
    // On x86 create an ioport resource and attempt to have the privilege bits
    // set for the process.
    zx::resource io;
    uint16_t io_base = 0xCF8;
    uint32_t io_size = 8; // CF8 - CFC (inclusive to 4 bytes each)
    char io_name[] = "ports!";
    ASSERT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_IOPORT, io_base,
                                   io_size, io_name, sizeof(io_name), &io),
              ZX_OK, "");
    EXPECT_EQ(zx_ioports_request(io.get(), io_base, io_size), ZX_OK, "");

    END_TEST;
}
#endif

BEGIN_TEST_CASE(resource_tests)
RUN_TEST(probe_address_space);
RUN_TEST(test_basic_actions);
RUN_TEST(test_exclusive_shared);
RUN_TEST(test_shared_exclusive);
RUN_TEST(test_invalid_args);
RUN_TEST(test_vmo_creation);
RUN_TEST(test_vmo_creation_smaller);
RUN_TEST(test_vmo_creation_unaligned);
#if defined(__x86_64__)
RUN_TEST(test_ioports);
#endif
END_TEST_CASE(resource_tests)
