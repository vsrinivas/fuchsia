// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/interrupt.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

extern "C" zx_handle_t get_root_resource(void);

static const size_t mmio_test_size = (PAGE_SIZE * 4);
static uint64_t mmio_test_base;

const zx::unowned_resource root() {
  // Please do not use get_root_resource() in new code. See ZX-1467.
  static zx_handle_t root = get_root_resource();
  return zx::unowned_resource(root);
}

// Physical memory is reserved during boot and its location varies based on
// system and architecture. What this 'test' does is scan MMIO space looking
// for a valid region to test against, ensuring that the only errors it sees
// are 'ZX_ERR_NOT_FOUND', which indicates that it is missing from the
// region allocator.
//
// TODO(fxbug.dev/32272): Figure out a way to test IRQs in the same manner, without
// hardcoding target-specific IRQ vectors in these tests. That information is
// stored in the kernel and is not exposed to userspace, so we can't simply
// guess/probe valid vectors like we can MMIO and still assume the tests are
// valid.

TEST(Resource, ProbeAddressSpace) {
  zx_status_t status;
  // Scan mmio in chunks until we find a gap that isn't exclusively reserved physical memory.
  uint64_t step = 0x100000000;
  for (uint64_t base = 0; base < UINT64_MAX - step; base += step) {
    zx::resource handle;
    status =
        zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, base, mmio_test_size, NULL, 0, &handle);
    if (status == ZX_OK) {
      mmio_test_base = base;
      break;
    }

    // If ZX_OK wasn't returned, then we should see ZX_ERR_NOT_FOUND and nothing else.
    ASSERT_EQ(ZX_ERR_NOT_FOUND, status);
  }
}

// This is a basic smoketest for creating resources and verifying the internals
// returned by zx_object_get_info match what the caller passed for creation.
TEST(Resource, BasicActions) {
  zx::resource new_root;
  zx_info_resource_t info;
  char root_name[] = "root";

  // Create a root and verify the fields are still zero, but the name matches.
  EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_ROOT, 0, 0, root_name, sizeof(root_name),
                                 &new_root),
            ZX_OK);
  ASSERT_EQ(new_root.get_info(ZX_INFO_RESOURCE, &info, sizeof(info), NULL, NULL), ZX_OK);
  EXPECT_EQ(info.kind, ZX_RSRC_KIND_ROOT);
  EXPECT_EQ(info.base, 0u);
  EXPECT_EQ(info.size, 0u);
  EXPECT_EQ(info.flags, 0u);
  EXPECT_EQ(0, strncmp(root_name, info.name, ZX_MAX_NAME_LEN));

  // Check that a resource is created with all the parameters passed to the syscall, and use
  // the new root resource created for good measure.
  zx::resource mmio;
  uint32_t kind = ZX_RSRC_KIND_MMIO;
  uint32_t flags = ZX_RSRC_FLAG_EXCLUSIVE;
  char mmio_name[] = "test_resource_name";
  ASSERT_EQ(zx::resource::create(new_root, kind | flags, mmio_test_base, mmio_test_size, mmio_name,
                                 sizeof(mmio_name), &mmio),
            ZX_OK);
  ASSERT_EQ(mmio.get_info(ZX_INFO_RESOURCE, &info, sizeof(info), NULL, NULL), ZX_OK);
  EXPECT_EQ(info.kind, kind);
  EXPECT_EQ(info.flags, flags);
  EXPECT_EQ(info.base, mmio_test_base);
  EXPECT_EQ(info.size, mmio_test_size);
  EXPECT_EQ(0, strncmp(info.name, mmio_name, ZX_MAX_NAME_LEN));
}

// This test covers every path that returns ZX_ERR_INVALID_ARGS from the syscall.
TEST(Resource, InvalidArgs) {
  zx::resource temp;
  zx::resource fail_hnd;
  // test privilege inversion by seeing if an MMIO resource can other resources.
  EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, mmio_test_base, mmio_test_size, NULL,
                                 0, &temp),
            ZX_OK);
  EXPECT_EQ(zx::resource::create(temp, ZX_RSRC_KIND_ROOT, 0, 0, NULL, 0, &fail_hnd),
            ZX_ERR_ACCESS_DENIED);
  EXPECT_EQ(zx::resource::create(temp, ZX_RSRC_KIND_IRQ, 0, 0, NULL, 0, &fail_hnd),
            ZX_ERR_ACCESS_DENIED);

  // test invalid kind
  EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_COUNT, mmio_test_base, mmio_test_size, NULL,
                                 0, &temp),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_COUNT + 1, mmio_test_base, mmio_test_size,
                                 NULL, 0, &temp),
            ZX_ERR_INVALID_ARGS);

  // test invalid base
  EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, UINT64_MAX, 1024, NULL, 0, &temp),
            ZX_ERR_INVALID_ARGS);
  // test invalid size
  EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, 1024, UINT64_MAX, NULL, 0, &temp),
            ZX_ERR_INVALID_ARGS);
  // test invalid options
  EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO | 0xFF0000, mmio_test_base,
                                 mmio_test_size, NULL, 0, &temp),
            ZX_ERR_INVALID_ARGS);
}

TEST(Resource, ExclusiveShared) {
  // Try to create a shared  resource and ensure it blocks an exclusive
  // resource.
  zx::resource mmio_1, mmio_2;
  EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO | ZX_RSRC_FLAG_EXCLUSIVE,
                                 mmio_test_base, mmio_test_size, NULL, 0, &mmio_1),
            ZX_OK);
  EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, mmio_test_base, mmio_test_size, NULL,
                                 0, &mmio_2),
            ZX_ERR_NOT_FOUND);
}

TEST(Resource, SharedExclusive) {
  // Try to create a shared resource and ensure it blocks an exclusive
  // resource.
  zx::resource mmio_1, mmio_2;
  EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, mmio_test_base, mmio_test_size, NULL,
                                 0, &mmio_1),
            ZX_OK);
  EXPECT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO | ZX_RSRC_FLAG_EXCLUSIVE,
                                 mmio_test_base, mmio_test_size, NULL, 0, &mmio_2),
            ZX_ERR_NOT_FOUND);
}

TEST(Resource, VmoCreation) {
  // Attempt to create a resource and then a vmo using that resource.
  zx::resource mmio;
  zx::vmo vmo;
  ASSERT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, mmio_test_base, mmio_test_size, NULL,
                                 0, &mmio),
            ZX_OK);
  EXPECT_EQ(
      zx_vmo_create_physical(mmio.get(), mmio_test_base, PAGE_SIZE, vmo.reset_and_get_address()),
      ZX_OK);
}

TEST(Resource, VmoCreationSmaller) {
  // Attempt to create a resource smaller than a page and ensure it still expands access to the
  // entire page.
  zx::resource mmio;
  zx::vmo vmo;
  ASSERT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, mmio_test_base, PAGE_SIZE / 2, NULL, 0,
                                 &mmio),
            ZX_OK);
  EXPECT_EQ(
      zx_vmo_create_physical(mmio.get(), mmio_test_base, PAGE_SIZE, vmo.reset_and_get_address()),
      ZX_OK);
}

TEST(Resource, VmoCreationUnaligned) {
  // Attempt to create an unaligned resource and ensure that the bounds are rounded appropriately
  // to the proper PAGE_SIZE.
  zx::resource mmio;
  zx::vmo vmo;
  ASSERT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, mmio_test_base + 0x7800, 0x2000, NULL,
                                 0, &mmio),
            ZX_OK);
  EXPECT_EQ(zx_vmo_create_physical(mmio.get(), mmio_test_base + 0x7000, 0x2000,
                                   vmo.reset_and_get_address()),
            ZX_OK);
}

// Returns zero on failure.
static zx_rights_t get_vmo_rights(const zx::vmo& vmo) {
  zx_info_handle_basic_t info;
  zx_status_t s =
      zx_object_get_info(vmo.get(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (s != ZX_OK) {
    EXPECT_EQ(s, ZX_OK);  // Poison the test
    return 0;
  }
  return info.rights;
}

TEST(Resource, VmoReplaceAsExecutable) {
  zx::resource vmex;
  zx::vmo vmo, vmo2, vmo3;

  // allocate an object
  ASSERT_EQ(ZX_OK, zx_vmo_create(PAGE_SIZE, 0, vmo.reset_and_get_address()));

  // set-exec with valid VMEX resource
  ASSERT_EQ(ZX_OK, zx::resource::create(*root(), ZX_RSRC_KIND_VMEX, 0, 0, NULL, 0, &vmex));
  ASSERT_EQ(ZX_OK, zx_handle_duplicate(vmo.get(), ZX_RIGHT_READ, vmo2.reset_and_get_address()));
  ASSERT_EQ(ZX_OK,
            zx_vmo_replace_as_executable(vmo2.get(), vmex.get(), vmo3.reset_and_get_address()));
  EXPECT_EQ(ZX_RIGHT_READ | ZX_RIGHT_EXECUTE, get_vmo_rights(vmo3));

  // set-exec with ZX_HANDLE_INVALID
  // TODO(mdempsky): Disallow.
  ASSERT_EQ(ZX_OK, zx_handle_duplicate(vmo.get(), ZX_RIGHT_READ, vmo2.reset_and_get_address()));
  ASSERT_EQ(ZX_OK, zx_vmo_replace_as_executable(vmo2.get(), ZX_HANDLE_INVALID,
                                                vmo3.reset_and_get_address()));
  EXPECT_EQ(ZX_RIGHT_READ | ZX_RIGHT_EXECUTE, get_vmo_rights(vmo3));

  // verify invalid handle fails
  ASSERT_EQ(ZX_OK, zx_handle_duplicate(vmo.get(), ZX_RIGHT_READ, vmo2.reset_and_get_address()));
  EXPECT_EQ(ZX_ERR_WRONG_TYPE,
            zx_vmo_replace_as_executable(vmo2.get(), vmo.get(), vmo3.reset_and_get_address()));
}

TEST(Resource, CreateResourceSlice) {
  {
    zx::resource mmio, smaller_mmio;
    ASSERT_EQ(ZX_OK, zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, mmio_test_base, PAGE_SIZE,
                                          NULL, 0, &mmio));
    // A new resource shouldn't be able to create ROOT.
    EXPECT_EQ(ZX_ERR_ACCESS_DENIED, zx::resource::create(mmio, ZX_RSRC_KIND_ROOT, mmio_test_base,
                                                         PAGE_SIZE, NULL, 0, &smaller_mmio));
    // Creating an identically sized resource with the wrong kind should fail.
    EXPECT_EQ(ZX_ERR_ACCESS_DENIED, zx::resource::create(mmio, ZX_RSRC_KIND_IRQ, mmio_test_base,
                                                         PAGE_SIZE, NULL, 0, &smaller_mmio));
    // Creating a resource with a different base and the same size should fail.
    EXPECT_EQ(ZX_ERR_ACCESS_DENIED,
              zx::resource::create(mmio, ZX_RSRC_KIND_IRQ, mmio_test_base + PAGE_SIZE, PAGE_SIZE,
                                   NULL, 0, &smaller_mmio));
    // Creating a resource with the same base and a different size should fail.
    EXPECT_EQ(ZX_ERR_ACCESS_DENIED, zx::resource::create(mmio, ZX_RSRC_KIND_IRQ, mmio_test_base,
                                                         PAGE_SIZE + 34u, NULL, 0, &smaller_mmio));
  }
  {
    // Try to make a slice going from exclusive -> shared. This should fail.
    zx::resource mmio, smaller_mmio;
    ASSERT_EQ(ZX_OK, zx::resource::create(*root(), ZX_RSRC_KIND_MMIO | ZX_RSRC_FLAG_EXCLUSIVE,
                                          mmio_test_base, PAGE_SIZE, NULL, 0, &mmio));
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, zx::resource::create(mmio, ZX_RSRC_KIND_MMIO, mmio_test_base,
                                                        PAGE_SIZE, NULL, 0, &smaller_mmio));
  }
  {
    // Try to make a slice going from shared -> exclusive. This should fail.
    zx::resource mmio, smaller_mmio;
    ASSERT_EQ(ZX_OK, zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, mmio_test_base, PAGE_SIZE,
                                          NULL, 0, &mmio));
    EXPECT_EQ(ZX_ERR_INVALID_ARGS,
              zx::resource::create(mmio, ZX_RSRC_KIND_MMIO | ZX_RSRC_FLAG_EXCLUSIVE, mmio_test_base,
                                   PAGE_SIZE, NULL, 0, &smaller_mmio));
  }
  {
    // Try to make a slice going from exclusive -> exclusive. This should fail.
    zx::resource mmio, smaller_mmio;
    ASSERT_EQ(ZX_OK, zx::resource::create(*root(), ZX_RSRC_KIND_MMIO | ZX_RSRC_FLAG_EXCLUSIVE,
                                          mmio_test_base, PAGE_SIZE, NULL, 0, &mmio));
    EXPECT_EQ(ZX_ERR_INVALID_ARGS,
              zx::resource::create(mmio, ZX_RSRC_KIND_MMIO | ZX_RSRC_FLAG_EXCLUSIVE, mmio_test_base,
                                   PAGE_SIZE, NULL, 0, &smaller_mmio));
  }
  {
    // Creating a identically sized resource should succeed.
    zx::resource mmio, smaller_mmio;
    ASSERT_EQ(ZX_OK, zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, mmio_test_base, PAGE_SIZE,
                                          NULL, 0, &mmio));
    EXPECT_EQ(ZX_OK, zx::resource::create(mmio, ZX_RSRC_KIND_MMIO, mmio_test_base, PAGE_SIZE, NULL,
                                          0, &smaller_mmio));
  }
  {
    // Creating an smaller resource should succeed.
    zx::vmo vmo;
    zx::resource mmio, smaller_mmio;
    EXPECT_EQ(ZX_OK, zx::resource::create(*root(), ZX_RSRC_KIND_MMIO, mmio_test_base, PAGE_SIZE * 2,
                                          NULL, 0, &mmio));
    // This will succeed at creating an MMIO resource that is a single page size.
    EXPECT_EQ(ZX_OK, zx::resource::create(mmio, ZX_RSRC_KIND_MMIO, mmio_test_base, PAGE_SIZE, NULL,
                                          0, &smaller_mmio));
    // Trying to create a VMO of the original size will fail
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE,
              zx_vmo_create_physical(smaller_mmio.get(), mmio_test_base, PAGE_SIZE * 2,
                                     vmo.reset_and_get_address()));
    // Trying to create VMO that fits in the resource will succeed.
    EXPECT_EQ(ZX_OK, zx_vmo_create_physical(smaller_mmio.get(), mmio_test_base, PAGE_SIZE,
                                            vmo.reset_and_get_address()));
  }
}

#if defined(__x86_64__)

static inline void outb(uint16_t port, uint8_t data) {
  __asm__ __volatile__("outb %1, %0" : : "dN"(port), "a"(data));
}

TEST(Resource, Ioports) {
  // On x86 create an ioport resource and attempt to have the privilege bits
  // set for the process.
  zx::resource io;
  uint16_t io_base = 0xCF8;
  uint32_t io_size = 8;  // CF8 - CFC (inclusive to 4 bytes each)
  char io_name[] = "ports!";
  ASSERT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_IOPORT, io_base, io_size, io_name,
                                 sizeof(io_name), &io),
            ZX_OK);
  EXPECT_EQ(zx_ioports_request(io.get(), io_base, io_size), ZX_OK);

  EXPECT_EQ(zx_ioports_release(io.get(), io_base, io_size), ZX_OK);

  zx::resource one_io;
  char one_io_name[] = "one";
  ASSERT_EQ(zx::resource::create(*root(), ZX_RSRC_KIND_IOPORT, 0x80, 1, one_io_name,
                                 strlen(one_io_name), &one_io),
            ZX_OK);
  // Ask for the wrong port. Should fail.
  EXPECT_EQ(zx_ioports_request(one_io.get(), io_base, io_size), ZX_ERR_OUT_OF_RANGE);
  // Lets get the right one.
  EXPECT_EQ(zx_ioports_request(one_io.get(), 0x80, 1), ZX_OK);

  outb(/*port=*/0x80, /*data=*/1);  // If we failed to get the port, this will #GP.

  // Try to release the wrong one.
  EXPECT_EQ(zx_ioports_release(one_io.get(), io_base, io_size), ZX_ERR_OUT_OF_RANGE);

  EXPECT_EQ(zx_ioports_release(one_io.get(), 0x80, 1), ZX_OK);
}

#endif  // defined(__x86_64__)
