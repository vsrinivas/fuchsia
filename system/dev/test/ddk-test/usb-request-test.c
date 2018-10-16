// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <ddk/protocol/usb.h>
#include <usb/usb-request.h>
#include <unittest/unittest.h>
#include <zircon/syscalls/iommu.h>

extern zx_handle_t get_root_resource(void);

static bool test_alloc_zero_size_request(void) {
    BEGIN_TEST;
    usb_request_t* req;
    ASSERT_EQ(usb_request_alloc(&req, PAGE_SIZE, 1, 0), ZX_ERR_INVALID_ARGS, "");
    END_TEST;
}

static bool test_alloc_simple(void) {
    BEGIN_TEST;
    zx_handle_t iommu_handle;
    zx_handle_t bti_handle;
    zx_iommu_desc_dummy_t desc;
    ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                              &iommu_handle), ZX_OK, "");
    ASSERT_EQ(zx_bti_create(iommu_handle, 0, 0, &bti_handle), ZX_OK, "");

    usb_request_t* req;
    ASSERT_EQ(usb_request_alloc(&req, PAGE_SIZE * 3, 1, sizeof(usb_request_t)), ZX_OK, "");
    ASSERT_NONNULL(req, "");
    ASSERT_TRUE((req->vmo_handle != ZX_HANDLE_INVALID), "");

    ASSERT_EQ(usb_request_physmap(req, bti_handle), ZX_OK, "");
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
    zx_iommu_desc_dummy_t desc;
    ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                              &iommu_handle), ZX_OK, "");
    zx_handle_t vmo;
    ASSERT_EQ(zx_vmo_create(PAGE_SIZE * 4, 0, &vmo), ZX_OK, "");

    usb_request_t* req;
    ASSERT_EQ(usb_request_alloc_vmo(&req, vmo, PAGE_SIZE, PAGE_SIZE * 3, 0,
                                    sizeof(usb_request_t)), ZX_OK, "");

    // Try copying some random data to and from the request.
    void* data = malloc(PAGE_SIZE * 4);
    ASSERT_EQ(usb_request_copy_to(req, data, PAGE_SIZE * 4, 0), PAGE_SIZE * 3,
              "only 3 pages should be copied as vmo_offset is 1 page");

    void* out_data = malloc(PAGE_SIZE * 4);
    ASSERT_EQ(usb_request_copy_from(req, out_data, PAGE_SIZE * 4, 0), PAGE_SIZE * 3,
              "only 3 pages should be copied as vmo_offset is 1 page");

    ASSERT_EQ(memcmp(data, out_data, PAGE_SIZE * 3), 0, "");

    free(data);
    free(out_data);
    usb_request_release(req);
    zx_handle_close(iommu_handle);
    END_TEST;
}

static bool test_pool(void) {
    BEGIN_TEST;
    zx_handle_t iommu_handle;
    zx_iommu_desc_dummy_t desc;
    ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                              &iommu_handle), ZX_OK, "");
    usb_request_t* req;
    ASSERT_EQ(usb_request_alloc(&req, 8u, 1, sizeof(usb_request_t)), ZX_OK, "");
    ASSERT_NONNULL(req, "");
    ASSERT_TRUE((req->vmo_handle != ZX_HANDLE_INVALID), "");

    usb_request_t* zero_req;
    ASSERT_EQ(usb_request_alloc(&zero_req, 0, 1, sizeof(usb_request_t)), ZX_OK, "");
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
    zx_handle_close(iommu_handle);
    END_TEST;
}

static bool test_phys_iter(void) {
    BEGIN_TEST;
    zx_handle_t iommu_handle;
    zx_handle_t bti_handle;
    zx_iommu_desc_dummy_t desc;
    ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                              &iommu_handle), ZX_OK, "");
    ASSERT_EQ(zx_bti_create(iommu_handle, 0, 0, &bti_handle), ZX_OK, "");

    phys_iter_t iter;
    usb_request_t* req;
    zx_paddr_t paddr;
    size_t length;
    size_t max_length;

    ASSERT_EQ(usb_request_alloc(&req, PAGE_SIZE * 4, 1, sizeof(usb_request_t)), ZX_OK, "");
    ASSERT_EQ(usb_request_physmap(req, bti_handle), ZX_OK, "");
    ASSERT_EQ(req->phys_count, 4u, "");
    // pretend that first two pages are contiguous and second two are not
    req->phys_list[1] = req->phys_list[0] + PAGE_SIZE;
    req->phys_list[2] = req->phys_list[0] + (PAGE_SIZE * 10);
    req->phys_list[3] = req->phys_list[0] + (PAGE_SIZE * 20);

    // simple discontiguous case
    max_length = req->header.length + PAGE_SIZE;
    usb_request_phys_iter_init(&iter, req, max_length);
    length = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(paddr, req->phys_list[0], "usb_request_phys_iter_next returned wrong paddr");
    ASSERT_EQ(length, (size_t)(PAGE_SIZE * 2), "usb_request_phys_iter_next returned wrong length");
    length = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(paddr, req->phys_list[2], "usb_request_phys_iter_next returned wrong paddr");
    ASSERT_EQ(length, (size_t)PAGE_SIZE, "usb_request_phys_iter_next returned wrong length");
    length = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(paddr, req->phys_list[3], "usb_request_phys_iter_next returned wrong paddr");
    ASSERT_EQ(length, (size_t)PAGE_SIZE, "usb_request_phys_iter_next returned wrong length");
    ASSERT_EQ(usb_request_phys_iter_next(&iter, &paddr), 0u, "");

    // discontiguous case with max_length < req->length
    max_length = PAGE_SIZE;
    usb_request_phys_iter_init(&iter, req, max_length);
    for (int i = 0; i < 4; i++) {
        length = usb_request_phys_iter_next(&iter, &paddr);
        ASSERT_EQ(paddr, req->phys_list[i], "usb_request_phys_iter_next returned wrong paddr");
        ASSERT_EQ(length, max_length, "usb_request_phys_iter_next returned wrong length");
    }
    ASSERT_EQ(usb_request_phys_iter_next(&iter, &paddr), 0u, "");

    // discontiguous case with unaligned vmo_offset and req->length
    req->offset = 100;
    max_length = req->header.length + PAGE_SIZE;
    req->header.length -= 1000;
    usb_request_phys_iter_init(&iter, req, max_length);
    size_t total_length = 0;
    length = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(paddr, req->phys_list[0] + req->offset, "");
    ASSERT_EQ(length, (size_t)(PAGE_SIZE * 2) - req->offset,
              "usb_request_phys_iter_next returned wrong length");
    total_length += length;
    length = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(paddr, req->phys_list[2], "");
    ASSERT_EQ(length, (size_t)PAGE_SIZE, "");
    total_length += length;
    length = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(paddr, req->phys_list[3], "");
    total_length += length;
    ASSERT_EQ(total_length, req->header.length, "");
    ASSERT_EQ(usb_request_phys_iter_next(&iter, &paddr), 0u, "");

    usb_request_release(req);

    END_TEST;
}

// Test behavior of merging adjacent single-page entries.
static bool test_phys_iter_merge(void) {
    BEGIN_TEST;

    usb_request_t* req;
    const size_t buf_size = 9 * PAGE_SIZE;

    zx_handle_t vmo_handle;
    ASSERT_EQ(zx_vmo_create(buf_size, 0, &vmo_handle), ZX_OK, "");

    usb_request_alloc_vmo(&req, vmo_handle, PAGE_SIZE, buf_size, 1, sizeof(usb_request_t));
    req->phys_list = malloc(sizeof(req->phys_list[0]) * 9);
    ASSERT_NONNULL(req->phys_list, "");
    req->phys_count = 9;
    req->phys_list[0] = 0x12345000;
    req->phys_list[1] = 0x12346000;
    req->phys_list[2] = 0x12347000;

    req->phys_list[3] = 0x12349000;

    req->phys_list[4] = 0x1234b000;

    req->phys_list[5] = 0x1234d000;
    req->phys_list[6] = 0x1234e000;
    req->phys_list[7] = 0x1234f000;
    req->phys_list[8] = 0x12350000;

    phys_iter_t iter;

    // Try iterating 3 pages at a time
    usb_request_phys_iter_init(&iter, req, 3 * PAGE_SIZE);
    zx_paddr_t paddr;
    size_t size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 3u * PAGE_SIZE, "");
    ASSERT_EQ(paddr, req->phys_list[0], "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, (size_t)PAGE_SIZE, "");
    ASSERT_EQ(paddr, req->phys_list[3], "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, (size_t)PAGE_SIZE, "");
    ASSERT_EQ(paddr, req->phys_list[4], "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 3u * PAGE_SIZE, "");
    ASSERT_EQ(paddr, req->phys_list[5], "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, (size_t)PAGE_SIZE, "");
    ASSERT_EQ(paddr, req->phys_list[8], "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 0u, "");

    // Now try iterating with no cap
    usb_request_phys_iter_init(&iter, req, 0);
    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 3u * PAGE_SIZE, "");
    ASSERT_EQ(paddr, req->phys_list[0], "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, (size_t)PAGE_SIZE, "");
    ASSERT_EQ(paddr, req->phys_list[3], "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, (size_t)PAGE_SIZE, "");
    ASSERT_EQ(paddr, req->phys_list[4], "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 4u * PAGE_SIZE, "");
    ASSERT_EQ(paddr, req->phys_list[5], "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 0u, "");

    free(req->phys_list);

    END_TEST;
}

// Test processing of a non-page-aligned contiguous backing buffer.
static bool test_phys_iter_unaligned_contig(void) {
    BEGIN_TEST;

    usb_request_t* req;
    const size_t buf_size = 4 * PAGE_SIZE;

    zx_handle_t vmo_handle;
    ASSERT_EQ(zx_vmo_create(buf_size, 0, &vmo_handle), ZX_OK, "");

    usb_request_alloc_vmo(&req, vmo_handle, 128, buf_size, 1, sizeof(usb_request_t));
    req->phys_list = malloc(sizeof(req->phys_list[0]) * 5);
    ASSERT_NONNULL(req->phys_list, "");
    req->phys_count = 5;
    req->phys_list[0] = 0x12345000;
    req->phys_list[1] = 0x12346000;
    req->phys_list[2] = 0x12347000;
    req->phys_list[3] = 0x12348000;
    req->phys_list[4] = 0x12349000;

    phys_iter_t iter;

    // Try iterating 3 pages at a time
    usb_request_phys_iter_init(&iter, req, 3 * PAGE_SIZE);
    zx_paddr_t paddr;
    size_t size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 3u * PAGE_SIZE - 128, "");
    ASSERT_EQ(paddr, req->phys_list[0] + 128, "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, PAGE_SIZE + 128u, "");
    ASSERT_EQ(paddr, req->phys_list[3], "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 0u, "");

    // Now try iterating with no cap
    usb_request_phys_iter_init(&iter, req, 0);
    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 4u * PAGE_SIZE, "");
    ASSERT_EQ(paddr, req->phys_list[0] + 128, "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 0u, "");

    free(req->phys_list);

    END_TEST;
}

// Test processing of a non-page-aligned non-contiguous backing buffer.
static bool test_phys_iter_unaligned_noncontig(void) {
    BEGIN_TEST;

    usb_request_t* req;
    const size_t buf_size = 2 * PAGE_SIZE;

    zx_handle_t vmo_handle;
    ASSERT_EQ(zx_vmo_create(buf_size, 0, &vmo_handle), ZX_OK, "");

    usb_request_alloc_vmo(&req, vmo_handle, 128, buf_size, 1, sizeof(usb_request_t));
    req->phys_list = malloc(sizeof(req->phys_list[0]) * 3);
    ASSERT_NONNULL(req->phys_list, "");
    req->phys_count = 3;
    req->phys_list[0] = 0x12345000;
    req->phys_list[1] = 0x12347000;
    req->phys_list[2] = 0x12349000;

    phys_iter_t iter;

    usb_request_phys_iter_init(&iter, req, 0);
    zx_paddr_t paddr;

    size_t size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, PAGE_SIZE - 128u, "");
    ASSERT_EQ(paddr, req->phys_list[0] + 128, "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, (size_t)PAGE_SIZE, "");
    ASSERT_EQ(paddr, req->phys_list[1], "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 128u, "");
    ASSERT_EQ(paddr, req->phys_list[2], "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 0u, "");

    free(req->phys_list);

    END_TEST;
}

// Test processing of a tiny page-aligned buffer.
static bool test_phys_iter_tiny_aligned(void) {
    BEGIN_TEST;

    usb_request_t* req;
    const size_t buf_size = 128;

    zx_handle_t vmo_handle;
    ASSERT_EQ(zx_vmo_create(buf_size, 0, &vmo_handle), ZX_OK, "");

    usb_request_alloc_vmo(&req, vmo_handle, 0, buf_size, 1, sizeof(usb_request_t));
    req->phys_list = malloc(sizeof(req->phys_list[0]) * 1);
    ASSERT_NONNULL(req->phys_list, "");
    req->phys_count = 1;
    req->phys_list[0] = 0x12345000;

    phys_iter_t iter;

    usb_request_phys_iter_init(&iter, req, 0);
    zx_paddr_t paddr;
    size_t size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 128u, "");
    ASSERT_EQ(paddr, req->phys_list[0], "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 0u, "");

    free(req->phys_list);

    END_TEST;
}

// Test processing of a tiny non-page-aligned buffer.
static bool test_phys_iter_tiny_unaligned(void) {
    BEGIN_TEST;

    usb_request_t* req;
    const size_t buf_size = 128;

    zx_handle_t vmo_handle;
    ASSERT_EQ(zx_vmo_create(buf_size, 0, &vmo_handle), ZX_OK, "");

    usb_request_alloc_vmo(&req, vmo_handle, 128, buf_size, 1, sizeof(usb_request_t));
    req->phys_list = malloc(sizeof(req->phys_list[0]) * 1);
    ASSERT_NONNULL(req->phys_list, "");
    req->phys_count = 1;
    req->phys_list[0] = 0x12345000;

    phys_iter_t iter;

    usb_request_phys_iter_init(&iter, req, 0);
    zx_paddr_t paddr;
    size_t size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 128u, "");
    ASSERT_EQ(paddr, req->phys_list[0] + 128, "");

    size = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(size, 0u, "");

    free(req->phys_list);

    END_TEST;
}

static bool test_set_sg_list(void) {
    BEGIN_TEST;
    usb_request_t* req;
    ASSERT_EQ(usb_request_alloc(&req, 3 * PAGE_SIZE, 1, sizeof(usb_request_t)), ZX_OK, "");
    // Wrap around the end of the request.
    usb_sg_entry_t wrapped[2] = {
        { .length = 10, .offset = (3 * PAGE_SIZE) - 10 },
        { .length = 50, .offset = 0 }
    };
    ASSERT_EQ(usb_request_set_sg_list(req, wrapped, 2), ZX_OK, "");
    ASSERT_EQ(req->header.length, 60u, "");

    usb_sg_entry_t unordered[3] = {
        { .length = 100, .offset = 2 * PAGE_SIZE },
        { .length = 50, .offset = 500 },
        { .length = 10, .offset = 2000 }
    };
    ASSERT_EQ(usb_request_set_sg_list(req, unordered, 3), ZX_OK, "");
    ASSERT_EQ(req->header.length, 160u, "");

    usb_request_release(req);
    END_TEST;
}

static bool test_invalid_sg_list(void) {
    BEGIN_TEST;
    zx_handle_t vmo;
    ASSERT_EQ(zx_vmo_create(PAGE_SIZE * 4, 0, &vmo), ZX_OK, "");

    usb_request_t* req;
    ASSERT_EQ(usb_request_alloc_vmo(&req, vmo, PAGE_SIZE, PAGE_SIZE * 3,
                                    0, sizeof(usb_request_t)), ZX_OK, "");

    usb_sg_entry_t out_of_bounds[1] = {
        { .length = 10, .offset = PAGE_SIZE * 3 }
    };
    ASSERT_NE(usb_request_set_sg_list(req, out_of_bounds, 1), ZX_OK, "entry ends past end of vmo");

    usb_sg_entry_t empty[1] = {
        { .length = 0, .offset = 0 }
    };
    ASSERT_NE(usb_request_set_sg_list(req, empty, 1), ZX_OK, "empty entry");

    usb_request_release(req);
    END_TEST;
}

BEGIN_TEST_CASE(usb_request_tests)
RUN_TEST(test_alloc_zero_size_request)
RUN_TEST(test_alloc_simple)
RUN_TEST(test_alloc_vmo)
RUN_TEST(test_pool)
RUN_TEST(test_phys_iter)
RUN_TEST(test_phys_iter_merge)
RUN_TEST(test_phys_iter_unaligned_contig)
RUN_TEST(test_phys_iter_unaligned_noncontig)
RUN_TEST(test_phys_iter_tiny_aligned)
RUN_TEST(test_phys_iter_tiny_unaligned)
RUN_TEST(test_set_sg_list)
RUN_TEST(test_invalid_sg_list)
END_TEST_CASE(usb_request_tests)

struct test_case_element* test_case_ddk_usb_request = TEST_CASE_ELEMENT(usb_request_tests);
