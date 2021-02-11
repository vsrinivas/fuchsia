// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/usb/c/banjo.h>
#include <lib/fake-bti/bti.h>
#include <limits.h>
#include <zircon/syscalls/iommu.h>

#include <ddk/hw/physiter/c/banjo.h>
#include <usb/usb-request.h>
#include <zxtest/zxtest.h>

namespace {

TEST(UsbRequestCTest, AllocZeroSizeRequest) {
  usb_request_t* req;
  ASSERT_EQ(usb_request_alloc(&req, PAGE_SIZE, 1, 0), ZX_ERR_INVALID_ARGS, "");
}

TEST(UsbRequestCTest, AllocSimple) {
  zx_handle_t bti_handle;
  ASSERT_OK(fake_bti_create(&bti_handle));

  usb_request_t* req;
  ASSERT_OK(usb_request_alloc(&req, PAGE_SIZE * 3, 1, sizeof(usb_request_t)));
  ASSERT_NOT_NULL(req, "");
  ASSERT_TRUE((req->vmo_handle != ZX_HANDLE_INVALID), "");

  ASSERT_OK(usb_request_physmap(req, bti_handle));
  ASSERT_NOT_NULL(req->phys_list, "expected phys list to be set");
  ASSERT_EQ(req->phys_count, 3u, "unexpected phys count");

  usb_request_release(req);
  zx_handle_close(bti_handle);
}

TEST(UsbRequestCTest, AllocVmo) {
  zx_handle_t vmo;
  ASSERT_OK(zx_vmo_create(PAGE_SIZE * 4, 0, &vmo));

  usb_request_t* req;
  ASSERT_EQ(usb_request_alloc_vmo(&req, vmo, PAGE_SIZE, PAGE_SIZE * 3, 0, sizeof(usb_request_t)),
            ZX_OK, "");

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
}

TEST(UsbRequestCTest, Pool) {
  usb_request_t* req;
  uint64_t req_size = sizeof(usb_request_t) + sizeof(usb_req_internal_t);
  ASSERT_OK(usb_request_alloc(&req, 8u, 1, req_size));
  ASSERT_NOT_NULL(req, "");
  ASSERT_TRUE((req->vmo_handle != ZX_HANDLE_INVALID), "");

  usb_request_t* zero_req;
  ASSERT_OK(usb_request_alloc(&zero_req, 0, 1, req_size));
  ASSERT_NOT_NULL(zero_req, "");

  usb_request_pool_t pool;
  usb_request_pool_init(&pool, sizeof(usb_request_t) + offsetof(usb_req_internal_t, node));

  ASSERT_OK(usb_request_pool_add(&pool, req));
  ASSERT_OK(usb_request_pool_add(&pool, zero_req));

  ASSERT_EQ(usb_request_pool_get(&pool, 0), zero_req, "");
  ASSERT_NULL(usb_request_pool_get(&pool, 0), "");
  ASSERT_EQ(usb_request_pool_get(&pool, 8u), req, "");
  ASSERT_NULL(usb_request_pool_get(&pool, 8u), "");

  usb_request_release(req);
  usb_request_release(zero_req);
}

TEST(UsbRequestCTest, PhysIter) {
  zx_handle_t bti_handle;
  ASSERT_OK(fake_bti_create(&bti_handle));

  phys_iter_t iter;
  usb_request_t* req;
  zx_paddr_t paddr;
  size_t length;
  size_t max_length;

  ASSERT_OK(usb_request_alloc(&req, PAGE_SIZE * 4, 1, sizeof(usb_request_t)));
  ASSERT_OK(usb_request_physmap(req, bti_handle));
  ASSERT_EQ(req->phys_count, 4u, "");
  // pretend that first two pages are contiguous and second two are not
  req->phys_list[1] = req->phys_list[0] + PAGE_SIZE;
  req->phys_list[2] = req->phys_list[0] + (PAGE_SIZE * 10);
  req->phys_list[3] = req->phys_list[0] + (PAGE_SIZE * 20);

  // simple discontiguous case
  max_length = req->header.length + PAGE_SIZE;
  usb_request_phys_iter_init(&iter, req, max_length);
  ASSERT_EQ(iter.total_iterated, 0u, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");
  length = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(paddr, req->phys_list[0], "usb_request_phys_iter_next returned wrong paddr");
  ASSERT_EQ(length, (size_t)(PAGE_SIZE * 2), "usb_request_phys_iter_next returned wrong length");
  ASSERT_EQ(iter.total_iterated, 2u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");
  length = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(paddr, req->phys_list[2], "usb_request_phys_iter_next returned wrong paddr");
  ASSERT_EQ(length, (size_t)PAGE_SIZE, "usb_request_phys_iter_next returned wrong length");
  ASSERT_EQ(iter.total_iterated, 3u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");
  length = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(paddr, req->phys_list[3], "usb_request_phys_iter_next returned wrong paddr");
  ASSERT_EQ(length, (size_t)PAGE_SIZE, "usb_request_phys_iter_next returned wrong length");
  ASSERT_EQ(iter.total_iterated, 4u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");
  ASSERT_EQ(usb_request_phys_iter_next(&iter, &paddr), 0u, "");
  ASSERT_EQ(iter.total_iterated, 4u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  // discontiguous case with max_length < req->length
  max_length = PAGE_SIZE;
  usb_request_phys_iter_init(&iter, req, max_length);
  for (int i = 0; i < 4; i++) {
    length = usb_request_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(paddr, req->phys_list[i], "usb_request_phys_iter_next returned wrong paddr");
    ASSERT_EQ(length, max_length, "usb_request_phys_iter_next returned wrong length");
    ASSERT_EQ(iter.total_iterated, max_length * (i + 1), "");
    ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");
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
  ASSERT_EQ(iter.total_iterated, PAGE_SIZE * 2 - req->offset, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  total_length += length;
  length = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(paddr, req->phys_list[2], "");
  ASSERT_EQ(length, (size_t)PAGE_SIZE, "");
  ASSERT_EQ(iter.total_iterated, (PAGE_SIZE * 3) - req->offset, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  total_length += length;
  length = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(paddr, req->phys_list[3], "");
  total_length += length;
  ASSERT_EQ(total_length, req->header.length, "");
  ASSERT_EQ(iter.total_iterated, req->header.length, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");
  ASSERT_EQ(usb_request_phys_iter_next(&iter, &paddr), 0u, "");
  ASSERT_EQ(iter.total_iterated, req->header.length, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  usb_request_release(req);
  zx_handle_close(bti_handle);
}

// Test behavior of merging adjacent single-page entries.
TEST(UsbRequestCTest, PhysIterMerge) {
  usb_request_t* req;
  const size_t buf_size = 9 * PAGE_SIZE;

  zx_handle_t vmo_handle;
  ASSERT_OK(zx_vmo_create(buf_size, 0, &vmo_handle));

  usb_request_alloc_vmo(&req, vmo_handle, PAGE_SIZE, buf_size, 1, sizeof(usb_request_t));
  req->phys_list = static_cast<zx_paddr_t*>(malloc(sizeof(req->phys_list[0]) * 9));
  ASSERT_NOT_NULL(req->phys_list, "");
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
  ASSERT_EQ(iter.total_iterated, 3u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, (size_t)PAGE_SIZE, "");
  ASSERT_EQ(paddr, req->phys_list[3], "");
  ASSERT_EQ(iter.total_iterated, 4u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, (size_t)PAGE_SIZE, "");
  ASSERT_EQ(paddr, req->phys_list[4], "");
  ASSERT_EQ(iter.total_iterated, 5u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 3u * PAGE_SIZE, "");
  ASSERT_EQ(paddr, req->phys_list[5], "");
  ASSERT_EQ(iter.total_iterated, 8u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, (size_t)PAGE_SIZE, "");
  ASSERT_EQ(paddr, req->phys_list[8], "");
  ASSERT_EQ(iter.total_iterated, 9u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 0u, "");
  ASSERT_EQ(iter.total_iterated, 9u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  // Now try iterating with no cap
  usb_request_phys_iter_init(&iter, req, 0);
  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 3u * PAGE_SIZE, "");
  ASSERT_EQ(paddr, req->phys_list[0], "");
  ASSERT_EQ(iter.total_iterated, 3u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, (size_t)PAGE_SIZE, "");
  ASSERT_EQ(paddr, req->phys_list[3], "");
  ASSERT_EQ(iter.total_iterated, 4u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, (size_t)PAGE_SIZE, "");
  ASSERT_EQ(paddr, req->phys_list[4], "");
  ASSERT_EQ(iter.total_iterated, 5u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 4u * PAGE_SIZE, "");
  ASSERT_EQ(paddr, req->phys_list[5], "");
  ASSERT_EQ(iter.total_iterated, 9u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 0u, "");
  ASSERT_EQ(iter.total_iterated, 9u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  usb_request_release(req);
}

// Test processing of a non-page-aligned contiguous backing buffer.
TEST(UsbRequestCTest, PhysIterUnalignedContig) {
  usb_request_t* req;
  const size_t buf_size = 4 * PAGE_SIZE;

  zx_handle_t vmo_handle;
  ASSERT_OK(zx_vmo_create(buf_size, 0, &vmo_handle));

  usb_request_alloc_vmo(&req, vmo_handle, 128, buf_size, 1, sizeof(usb_request_t));
  req->phys_list = static_cast<zx_paddr_t*>(malloc(sizeof(req->phys_list[0]) * 5));
  ASSERT_NOT_NULL(req->phys_list, "");
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
  ASSERT_EQ(iter.total_iterated, 3u * PAGE_SIZE - 128, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, PAGE_SIZE + 128u, "");
  ASSERT_EQ(paddr, req->phys_list[3], "");
  ASSERT_EQ(iter.total_iterated, 4u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 0u, "");
  ASSERT_EQ(iter.total_iterated, 4u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  // Now try iterating with no cap
  usb_request_phys_iter_init(&iter, req, 0);
  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 4u * PAGE_SIZE, "");
  ASSERT_EQ(paddr, req->phys_list[0] + 128, "");
  ASSERT_EQ(iter.total_iterated, 4u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 0u, "");
  ASSERT_EQ(iter.total_iterated, 4u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  usb_request_release(req);
}

// Test processing of a non-page-aligned non-contiguous backing buffer.
TEST(UsbRequestCTest, PhysIterUnalignedNoncontig) {
  usb_request_t* req;
  const size_t buf_size = 2 * PAGE_SIZE;

  zx_handle_t vmo_handle;
  ASSERT_OK(zx_vmo_create(buf_size, 0, &vmo_handle));

  usb_request_alloc_vmo(&req, vmo_handle, 128, buf_size, 1, sizeof(usb_request_t));
  req->phys_list = static_cast<zx_paddr_t*>(malloc(sizeof(req->phys_list[0]) * 3));
  ASSERT_NOT_NULL(req->phys_list, "");
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
  ASSERT_EQ(iter.total_iterated, PAGE_SIZE - 128u, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, (size_t)PAGE_SIZE, "");
  ASSERT_EQ(paddr, req->phys_list[1], "");
  ASSERT_EQ(iter.total_iterated, (2 * PAGE_SIZE) - 128u, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 128u, "");
  ASSERT_EQ(paddr, req->phys_list[2], "");
  ASSERT_EQ(iter.total_iterated, 2u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 0u, "");
  ASSERT_EQ(iter.total_iterated, 2u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  usb_request_release(req);
}

// Test processing of a tiny page-aligned buffer.
TEST(UsbRequestCTest, PhysIterTinyAligned) {
  usb_request_t* req;
  const size_t buf_size = 128;

  zx_handle_t vmo_handle;
  ASSERT_OK(zx_vmo_create(buf_size, 0, &vmo_handle));

  usb_request_alloc_vmo(&req, vmo_handle, 0, buf_size, 1, sizeof(usb_request_t));
  req->phys_list = static_cast<zx_paddr_t*>(malloc(sizeof(req->phys_list[0]) * 1));
  ASSERT_NOT_NULL(req->phys_list, "");
  req->phys_count = 1;
  req->phys_list[0] = 0x12345000;

  phys_iter_t iter;

  usb_request_phys_iter_init(&iter, req, 0);
  zx_paddr_t paddr;
  size_t size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 128u, "");
  ASSERT_EQ(paddr, req->phys_list[0], "");
  ASSERT_EQ(iter.total_iterated, 128u, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 0u, "");
  ASSERT_EQ(iter.total_iterated, 128u, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  usb_request_release(req);
}

// Test processing of a tiny non-page-aligned buffer.
TEST(UsbRequestCTest, PhysIterTinyUnaligned) {
  usb_request_t* req;
  const size_t buf_size = 128;

  zx_handle_t vmo_handle;
  ASSERT_OK(zx_vmo_create(buf_size, 0, &vmo_handle));

  usb_request_alloc_vmo(&req, vmo_handle, 128, buf_size, 1, sizeof(usb_request_t));
  req->phys_list = static_cast<zx_paddr_t*>(malloc(sizeof(req->phys_list[0]) * 1));
  ASSERT_NOT_NULL(req->phys_list, "");
  req->phys_count = 1;
  req->phys_list[0] = 0x12345000;

  phys_iter_t iter;

  usb_request_phys_iter_init(&iter, req, 0);
  zx_paddr_t paddr;
  size_t size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 128u, "");
  ASSERT_EQ(paddr, req->phys_list[0] + 128, "");
  ASSERT_EQ(iter.total_iterated, 128u, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 0u, "");
  ASSERT_EQ(iter.total_iterated, 128u, "");
  ASSERT_EQ(iter.offset, iter.total_iterated, "offset == total_iterated for non scatter gather");

  usb_request_release(req);
}

TEST(UsbRequestCTest, SetSgList) {
  usb_request_t* req;
  ASSERT_OK(usb_request_alloc(&req, 3 * PAGE_SIZE, 1, sizeof(usb_request_t)));
  // Wrap around the end of the request.
  sg_entry_t wrapped[2] = {{.length = 10, .offset = (3 * PAGE_SIZE) - 10},
                           {.length = 50, .offset = 0}};
  ASSERT_OK(usb_request_set_sg_list(req, wrapped, 2));
  ASSERT_EQ(req->header.length, 60u, "");

  sg_entry_t unordered[3] = {{.length = 100, .offset = 2 * PAGE_SIZE},
                             {.length = 50, .offset = 500},
                             {.length = 10, .offset = 2000}};
  ASSERT_OK(usb_request_set_sg_list(req, unordered, 3));
  ASSERT_EQ(req->header.length, 160u, "");

  usb_request_release(req);
}

TEST(UsbRequestCTest, InvalidSgList) {
  zx_handle_t vmo;
  ASSERT_OK(zx_vmo_create(PAGE_SIZE * 4, 0, &vmo));

  usb_request_t* req;
  ASSERT_EQ(usb_request_alloc_vmo(&req, vmo, PAGE_SIZE, PAGE_SIZE * 3, 0, sizeof(usb_request_t)),
            ZX_OK, "");

  sg_entry_t out_of_bounds[1] = {{.length = 10, .offset = PAGE_SIZE * 3}};
  ASSERT_NE(usb_request_set_sg_list(req, out_of_bounds, 1), ZX_OK, "entry ends past end of vmo");

  sg_entry_t empty[1] = {{.length = 0, .offset = 0}};
  ASSERT_NE(usb_request_set_sg_list(req, empty, 1), ZX_OK, "empty entry");

  usb_request_release(req);
}

// Test processing of a page-aligned contiguous backing buffer with a scatter gather list.
TEST(UsbRequestCTest, PhysIterSgAlignedContig) {
  usb_request_t* req;
  const size_t buf_size = 5 * PAGE_SIZE;

  zx_handle_t vmo_handle;
  ASSERT_OK(zx_vmo_create(buf_size, 0, &vmo_handle));

  usb_request_alloc_vmo(&req, vmo_handle, PAGE_SIZE, buf_size, 1, sizeof(usb_request_t));
  req->phys_list = static_cast<zx_paddr_t*>(malloc(sizeof(req->phys_list[0]) * 4));
  ASSERT_NOT_NULL(req->phys_list, "");
  req->phys_count = 4;
  req->phys_list[0] = 0x12345000;
  req->phys_list[1] = 0x12346000;
  req->phys_list[2] = 0x12347000;
  req->phys_list[3] = 0x12348000;

  sg_entry_t sg_list[3] = {{.length = 100, .offset = 0},
                           {.length = 2 * PAGE_SIZE, .offset = 500},
                           {.length = PAGE_SIZE - 100, .offset = 3 * PAGE_SIZE}};
  ASSERT_OK(usb_request_set_sg_list(req, sg_list, 3));

  phys_iter_t iter;

  usb_request_phys_iter_init(&iter, req, 0);
  zx_paddr_t paddr;
  size_t size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 100u, "first scatter gather entry");
  ASSERT_EQ(paddr, req->phys_list[0], "first scatter gather entry");
  ASSERT_EQ(iter.total_iterated, 100u, "");
  ASSERT_EQ(iter.offset, 100u, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 2u * PAGE_SIZE, "second scatter gather entry");
  ASSERT_EQ(paddr, req->phys_list[0] + 500u, "second scatter gather entry");
  ASSERT_EQ(iter.total_iterated, (2u * PAGE_SIZE) + 100, "");
  ASSERT_EQ(iter.offset, 2u * PAGE_SIZE, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, PAGE_SIZE - 100u, "third scatter gather entry");
  ASSERT_EQ(paddr, req->phys_list[3], "third scatter gather entry");
  ASSERT_EQ(iter.total_iterated, 3u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, PAGE_SIZE - 100u, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 0u, "no more scatter gather entries");
  ASSERT_EQ(iter.total_iterated, 3u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, PAGE_SIZE - 100u, "");

  usb_request_release(req);
}

// Test processing of a page-aligned non-contiguous backing buffer with a scatter gather list.
TEST(UsbRequestCTest, PhysIterSgAlignedNoncontig) {
  usb_request_t* req;
  const size_t buf_size = 6 * PAGE_SIZE;

  zx_handle_t vmo_handle;
  ASSERT_OK(zx_vmo_create(buf_size, 0, &vmo_handle));

  usb_request_alloc_vmo(&req, vmo_handle, PAGE_SIZE * 2, buf_size, 1, sizeof(usb_request_t));
  req->phys_list = static_cast<zx_paddr_t*>(malloc(sizeof(req->phys_list[0]) * 4));
  ASSERT_NOT_NULL(req->phys_list, "");
  req->phys_count = 4;
  req->phys_list[0] = 0x12341000;
  req->phys_list[1] = 0x12343000;
  req->phys_list[2] = 0x12345000;
  req->phys_list[3] = 0x12347000;

  sg_entry_t sg_list[2] = {{.length = PAGE_SIZE, .offset = (2 * PAGE_SIZE) + 128},
                           {.length = 2 * PAGE_SIZE, .offset = 10}};
  ASSERT_OK(usb_request_set_sg_list(req, sg_list, 2));

  phys_iter_t iter;

  usb_request_phys_iter_init(&iter, req, 0);
  zx_paddr_t paddr;
  size_t size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, PAGE_SIZE - 128u, "first scatter gather entry: part 1");
  ASSERT_EQ(paddr, req->phys_list[2] + 128u, "first scatter gather entry: part 1");
  ASSERT_EQ(iter.total_iterated, PAGE_SIZE - 128u, "");
  ASSERT_EQ(iter.offset, PAGE_SIZE - 128u, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 128u, "first scatter gather entry: part 2");
  ASSERT_EQ(paddr, req->phys_list[3], "first scatter gather entry: part 2");
  ASSERT_EQ(iter.total_iterated, 1u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, 1u * PAGE_SIZE, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, PAGE_SIZE - 10u, "second scatter gather entry: part 1");
  ASSERT_EQ(paddr, req->phys_list[0] + 10u, "second scatter gather entry: part 1");
  ASSERT_EQ(iter.total_iterated, (2u * PAGE_SIZE) - 10u, "");
  ASSERT_EQ(iter.offset, PAGE_SIZE - 10u, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 1u * PAGE_SIZE, "second scatter gather entry: part 2");
  ASSERT_EQ(paddr, req->phys_list[1], "second scatter gather entry: part 2");
  ASSERT_EQ(iter.total_iterated, (3u * PAGE_SIZE) - 10u, "");
  ASSERT_EQ(iter.offset, (2u * PAGE_SIZE) - 10u, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 10u, "second scatter gather entry: part 3");
  ASSERT_EQ(paddr, req->phys_list[2], "second scatter gather entry: part 3");
  ASSERT_EQ(iter.total_iterated, 3u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, 2u * PAGE_SIZE, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 0u, "no more scatter gather entries");
  ASSERT_EQ(iter.total_iterated, 3u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, 2u * PAGE_SIZE, "");

  usb_request_release(req);
}

// Test processing of a non-page-aligned contiguous backing buffer with a scatter gather list.
TEST(UsbRequestCTest, PhysIterSgUnalignedContig) {
  usb_request_t* req;
  const size_t buf_size = 7 * PAGE_SIZE;

  zx_handle_t vmo_handle;
  ASSERT_OK(zx_vmo_create(buf_size, 0, &vmo_handle));

  usb_request_alloc_vmo(&req, vmo_handle, PAGE_SIZE + 3000, buf_size, 1, sizeof(usb_request_t));
  req->phys_list = static_cast<zx_paddr_t*>(malloc(sizeof(req->phys_list[0]) * 6));
  ASSERT_NOT_NULL(req->phys_list, "");
  req->phys_count = 6;
  req->phys_list[0] = 0x12345000;
  req->phys_list[1] = 0x12346000;
  req->phys_list[2] = 0x12347000;
  req->phys_list[3] = 0x12348000;
  req->phys_list[4] = 0x12349000;
  req->phys_list[5] = 0x1234a000;

  sg_entry_t sg_list[2] = {{.length = 4000, .offset = 2 * PAGE_SIZE},
                           {.length = 5000, .offset = (3 * PAGE_SIZE) + 1000}};
  ASSERT_OK(usb_request_set_sg_list(req, sg_list, 2));

  phys_iter_t iter;

  usb_request_phys_iter_init(&iter, req, 0);
  zx_paddr_t paddr;
  size_t size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 4000u, "first scatter gather entry");
  ASSERT_EQ(paddr, req->phys_list[2] + 3000u, "first scatter gather entry");
  ASSERT_EQ(iter.total_iterated, 4000u, "");
  ASSERT_EQ(iter.offset, 4000u, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 5000u, "second scatter gather entry");
  ASSERT_EQ(paddr, req->phys_list[3] + 4000u, "second scatter gather entry");
  ASSERT_EQ(iter.total_iterated, 9000u, "");
  ASSERT_EQ(iter.offset, 5000u, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 0u, "no more scatter gather entries");
  ASSERT_EQ(iter.total_iterated, 9000u, "");
  ASSERT_EQ(iter.offset, 5000u, "");

  usb_request_release(req);
}

// Test processing of a non-page-aligned non-contiguous backing buffer with a scatter gather list.
TEST(UsbRequestCTest, PhysIterSgUnalignedNoncontig) {
  usb_request_t* req;
  const size_t buf_size = 5 * PAGE_SIZE;

  zx_handle_t vmo_handle;
  ASSERT_OK(zx_vmo_create(buf_size, 0, &vmo_handle));

  usb_request_alloc_vmo(&req, vmo_handle, 128, buf_size, 1, sizeof(usb_request_t));
  req->phys_list = static_cast<zx_paddr_t*>(malloc(sizeof(req->phys_list[0]) * 6));
  ASSERT_NOT_NULL(req->phys_list, "");
  req->phys_count = 6;
  req->phys_list[0] = 0x12345000;
  req->phys_list[1] = 0x12347000;
  req->phys_list[2] = 0x12349000;
  req->phys_list[3] = 0x1234b000;
  req->phys_list[4] = 0x1234d000;
  req->phys_list[5] = 0x1234f000;

  sg_entry_t sg_list[2] = {{.length = PAGE_SIZE, .offset = (3 * PAGE_SIZE) + 1},
                           {.length = 2 * PAGE_SIZE, .offset = PAGE_SIZE}};
  ASSERT_OK(usb_request_set_sg_list(req, sg_list, 2));

  phys_iter_t iter;

  usb_request_phys_iter_init(&iter, req, 0);
  zx_paddr_t paddr;
  size_t size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, PAGE_SIZE - 129u, "first scatter gather entry: part 1");
  ASSERT_EQ(paddr, req->phys_list[3] + 129u, "first scatter gather entry: part 1");
  ASSERT_EQ(iter.total_iterated, PAGE_SIZE - 129u, "");
  ASSERT_EQ(iter.offset, PAGE_SIZE - 129u, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 129u, "first scatter gather entry: part 2");
  ASSERT_EQ(paddr, req->phys_list[4], "first scatter gather entry: part 2");
  ASSERT_EQ(iter.total_iterated, 1u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, 1u * PAGE_SIZE, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, PAGE_SIZE - 128u, "second scatter gather entry: part 1");
  ASSERT_EQ(paddr, req->phys_list[1] + 128u, "second scatter gather entry: part 1");
  ASSERT_EQ(iter.total_iterated, (2u * PAGE_SIZE) - 128u, "");
  ASSERT_EQ(iter.offset, PAGE_SIZE - 128u, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 1u * PAGE_SIZE, "second scatter gather entry: part 2");
  ASSERT_EQ(paddr, req->phys_list[2], "second scatter gather entry: part 2");
  ASSERT_EQ(iter.total_iterated, (3u * PAGE_SIZE) - 128u, "");
  ASSERT_EQ(iter.offset, (2u * PAGE_SIZE) - 128u, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 128u, "second scatter gather entry: part 3");
  ASSERT_EQ(paddr, req->phys_list[3], "second scatter gather entry: part 3");
  ASSERT_EQ(iter.total_iterated, 3u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, 2u * PAGE_SIZE, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 0u, "no more scatter gather entries");
  ASSERT_EQ(iter.total_iterated, 3u * PAGE_SIZE, "");
  ASSERT_EQ(iter.offset, 2u * PAGE_SIZE, "");

  usb_request_release(req);
}

// Test processing of a tiny page-aligned buffer with a scatter gather list.
TEST(UsbRequestCTest, PhysIterSgTinyAligned) {
  usb_request_t* req;
  const size_t buf_size = 128;

  zx_handle_t vmo_handle;
  ASSERT_OK(zx_vmo_create(buf_size, 0, &vmo_handle));

  usb_request_alloc_vmo(&req, vmo_handle, 0, buf_size, 1, sizeof(usb_request_t));
  req->phys_list = static_cast<zx_paddr_t*>(malloc(sizeof(req->phys_list[0]) * 1));
  ASSERT_NOT_NULL(req->phys_list, "");
  req->phys_count = 1;
  req->phys_list[0] = 0x12345000;

  sg_entry_t sg_list[2] = {{.length = 10, .offset = 0}, {.length = 20, .offset = 100}};
  ASSERT_OK(usb_request_set_sg_list(req, sg_list, 2));

  phys_iter_t iter;

  usb_request_phys_iter_init(&iter, req, 0);
  zx_paddr_t paddr;
  size_t size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 10u, "first scatter gather entry");
  ASSERT_EQ(paddr, req->phys_list[0], "first scatter gather entry");
  ASSERT_EQ(iter.total_iterated, 10u, "");
  ASSERT_EQ(iter.offset, 10u, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 20u, "second scatter gather entry");
  ASSERT_EQ(paddr, req->phys_list[0] + 100u, "second scatter gather entry");
  ASSERT_EQ(iter.total_iterated, 30u, "");
  ASSERT_EQ(iter.offset, 20u, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 0u, "no more scatter gather entries");
  ASSERT_EQ(iter.total_iterated, 30u, "");
  ASSERT_EQ(iter.offset, 20u, "");

  usb_request_release(req);
}

// Test processing of a tiny non-page-aligned buffer with a scatter gather list.
TEST(UsbRequestCTest, PhysIterSgTinyUnaligned) {
  usb_request_t* req;
  const size_t buf_size = 128;

  zx_handle_t vmo_handle;
  ASSERT_OK(zx_vmo_create(PAGE_SIZE, 0, &vmo_handle));

  usb_request_alloc_vmo(&req, vmo_handle, 128, buf_size, 1, sizeof(usb_request_t));
  req->phys_list = static_cast<zx_paddr_t*>(malloc(sizeof(req->phys_list[0]) * 1));
  ASSERT_NOT_NULL(req->phys_list, "");
  req->phys_count = 1;
  req->phys_list[0] = 0x12345000;

  sg_entry_t sg_list[2] = {{.length = 10, .offset = 0}, {.length = 20, .offset = 128}};
  ASSERT_OK(usb_request_set_sg_list(req, sg_list, 2));

  phys_iter_t iter;

  usb_request_phys_iter_init(&iter, req, 0);
  zx_paddr_t paddr;
  size_t size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 10u, "first scatter gather entry");
  ASSERT_EQ(paddr, req->phys_list[0] + 128u, "first scatter gather entry");
  ASSERT_EQ(iter.total_iterated, 10u, "");
  ASSERT_EQ(iter.offset, 10u, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 20u, "second scatter gather entry");
  ASSERT_EQ(paddr, req->phys_list[0] + 256u, "second scatter gather entry");
  ASSERT_EQ(iter.total_iterated, 30u, "");
  ASSERT_EQ(iter.offset, 20u, "");

  size = usb_request_phys_iter_next(&iter, &paddr);
  ASSERT_EQ(size, 0u, "no more scatter gather entries");
  ASSERT_EQ(iter.total_iterated, 30u, "");
  ASSERT_EQ(iter.offset, 20u, "");

  usb_request_release(req);
}

}  // namespace
