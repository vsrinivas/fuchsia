// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <ddk/protocol/usb.h>
#include <ddk/usb-request.h>
#include <unittest/unittest.h>
#include <zircon/syscalls/iommu.h>

extern zx_handle_t get_root_resource(void);

static bool test_alloc_simple(void) {
    BEGIN_TEST;
    zx_handle_t iommu_handle;
    zx_handle_t bti_handle;
    zx_iommu_desc_dummy_t desc;
    ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                              &iommu_handle), ZX_OK, "");
    ASSERT_EQ(zx_bti_create(iommu_handle, 0, 0, &bti_handle), ZX_OK, "");

    usb_request_t* req;
    ASSERT_EQ(usb_request_alloc(&req, bti_handle, PAGE_SIZE * 3, 1), ZX_OK, "");
    ASSERT_NONNULL(req, "");
    ASSERT_TRUE((req->vmo_handle != ZX_HANDLE_INVALID), "");

    ASSERT_EQ(usb_request_physmap(req), ZX_OK, "");
    ASSERT_NONNULL(req->phys_list, "expected phys list to be set");
    ASSERT_EQ(req->phys_count, 3u, "unexpected phys count");

    usb_request_release(req);
    zx_handle_close(bti_handle);
    zx_handle_close(iommu_handle);
    END_TEST;
}

static bool test_alloc_vmo(void) {
    BEGIN_TEST;
    zx_handle_t iommu_handle;
    zx_handle_t bti_handle;
    zx_iommu_desc_dummy_t desc;
    ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                              &iommu_handle), ZX_OK, "");
    ASSERT_EQ(zx_bti_create(iommu_handle, 0, 0, &bti_handle), ZX_OK, "");

    zx_handle_t vmo;
    ASSERT_EQ(zx_vmo_create(PAGE_SIZE * 4, 0, &vmo), ZX_OK, "");

    usb_request_t* req;
    ASSERT_EQ(usb_request_alloc_vmo(&req, bti_handle, vmo, PAGE_SIZE, PAGE_SIZE * 3, 0), ZX_OK, "");

    // Try copying some random data to and from the request.
    void* data = malloc(PAGE_SIZE * 4);
    ASSERT_EQ(usb_request_copyto(req, data, PAGE_SIZE * 4, 0), PAGE_SIZE * 3,
              "only 3 pages should be copied as vmo_offset is 1 page");

    void* out_data = malloc(PAGE_SIZE * 4);
    ASSERT_EQ(usb_request_copyfrom(req, out_data, PAGE_SIZE * 4, 0), PAGE_SIZE * 3,
              "only 3 pages should be copied as vmo_offset is 1 page");

    ASSERT_EQ(memcmp(data, out_data, PAGE_SIZE * 3), 0, "");

    free(data);
    free(out_data);
    usb_request_release(req);
    zx_handle_close(bti_handle);
    zx_handle_close(iommu_handle);
    END_TEST;
}

static bool test_pool(void) {
    BEGIN_TEST;
    zx_handle_t iommu_handle;
    zx_handle_t bti_handle;
    zx_iommu_desc_dummy_t desc;
    ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                              &iommu_handle), ZX_OK, "");
    ASSERT_EQ(zx_bti_create(iommu_handle, 0, 0, &bti_handle), ZX_OK, "");

    usb_request_t* req;
    ASSERT_EQ(usb_request_alloc(&req, bti_handle, 8u, 1), ZX_OK, "");
    ASSERT_NONNULL(req, "");
    ASSERT_TRUE((req->vmo_handle != ZX_HANDLE_INVALID), "");

    usb_request_t* zero_req;
    ASSERT_EQ(usb_request_alloc(&zero_req, bti_handle, 0, 1), ZX_OK, "");
    ASSERT_NONNULL(zero_req, "");

    usb_request_pool_t pool;
    usb_request_pool_init(&pool);

    usb_request_pool_add(&pool, req);
    usb_request_pool_add(&pool, zero_req);

    ASSERT_EQ(usb_request_pool_get(&pool, 0), zero_req, "");
    ASSERT_EQ(usb_request_pool_get(&pool, 0), NULL, "");
    ASSERT_EQ(usb_request_pool_get(&pool, 8u), req, "");
    ASSERT_EQ(usb_request_pool_get(&pool, 8u), NULL, "");

    usb_request_release(req);
    usb_request_release(zero_req);
    zx_handle_close(bti_handle);
    zx_handle_close(iommu_handle);
    END_TEST;
}

BEGIN_TEST_CASE(usb_request_tests)
RUN_TEST(test_alloc_simple)
RUN_TEST(test_alloc_vmo)
RUN_TEST(test_pool)
END_TEST_CASE(usb_request_tests)

struct test_case_element* test_case_ddk_usb_request = TEST_CASE_ELEMENT(usb_request_tests);
