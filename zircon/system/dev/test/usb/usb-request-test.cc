// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/protocol/usb/function.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <lib/fake-bti/bti.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>
#include <zxtest/zxtest.h>

#include "usb/request-cpp.h"

namespace {

using Request = usb::Request<void>;

constexpr size_t kParentReqSize = sizeof(usb_request_t);
constexpr usb_request_complete_t kNoCallback = {};

TEST(UsbRequestTest, Alloc) {
  std::optional<Request> request;
  EXPECT_OK(Request::Alloc(&request, 0, 0, kParentReqSize));
}

TEST(UsbRequestTest, Init) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  std::optional<Request> request;
  ASSERT_OK(Request::Alloc(&request, 0, 0, kParentReqSize));
  EXPECT_OK(request->Init(vmo, 0, 0, 0));
}

TEST(UsbRequestTest, AllocVmo) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  std::optional<Request> request;
  EXPECT_OK(Request::AllocVmo(&request, vmo, 0, 0, 0, kParentReqSize));
}

TEST(UsbRequestTest, Copy) {
  std::optional<Request> request;
  EXPECT_EQ(Request::Alloc(&request, ZX_PAGE_SIZE, 0, kParentReqSize), ZX_OK);

  constexpr uint8_t kSampleData[] = "blahblahblah";
  EXPECT_EQ(request->CopyTo(kSampleData, sizeof(kSampleData), 10), sizeof(kSampleData));
  uint8_t data[sizeof(kSampleData)] = {};
  EXPECT_EQ(request->CopyFrom(data, sizeof(data), 10), sizeof(data));
  EXPECT_EQ(memcmp(data, kSampleData, sizeof(kSampleData)), 0);
}

TEST(UsbRequestTest, Mmap) {
  std::optional<Request> request;
  EXPECT_EQ(Request::Alloc(&request, ZX_PAGE_SIZE, 0, kParentReqSize), ZX_OK);

  constexpr uint8_t kSampleData[] = "blahblahblah";
  EXPECT_EQ(request->CopyTo(kSampleData, sizeof(kSampleData), 10), sizeof(kSampleData));
  void* data = nullptr;
  EXPECT_EQ(request->Mmap(&data), ZX_OK);
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(memcmp(&static_cast<uint8_t*>(data)[10], kSampleData, sizeof(kSampleData)), 0);
}

TEST(UsbRequestTest, CacheOp) {
  std::optional<Request> request;
  EXPECT_EQ(Request::Alloc(&request, ZX_PAGE_SIZE, 0, kParentReqSize), ZX_OK);

  EXPECT_EQ(request->CacheOp(USB_REQUEST_CACHE_INVALIDATE, 0, 0), ZX_OK);
  EXPECT_EQ(request->CacheOp(USB_REQUEST_CACHE_INVALIDATE, 10, 10), ZX_OK);
  EXPECT_EQ(request->CacheOp(USB_REQUEST_CACHE_CLEAN, 0, 0), ZX_OK);
  EXPECT_EQ(request->CacheOp(USB_REQUEST_CACHE_CLEAN, 10, 10), ZX_OK);
  EXPECT_EQ(request->CacheOp(USB_REQUEST_CACHE_CLEAN_INVALIDATE, 0, 0), ZX_OK);
  EXPECT_EQ(request->CacheOp(USB_REQUEST_CACHE_CLEAN_INVALIDATE, 10, 10), ZX_OK);
  EXPECT_EQ(request->CacheOp(USB_REQUEST_CACHE_SYNC, 0, 0), ZX_OK);
  EXPECT_EQ(request->CacheOp(USB_REQUEST_CACHE_SYNC, 10, 10), ZX_OK);
}

TEST(UsbRequestTest, CacheFlush) {
  std::optional<Request> request;
  EXPECT_EQ(Request::Alloc(&request, ZX_PAGE_SIZE, 0, kParentReqSize), ZX_OK);

  EXPECT_EQ(request->CacheFlush(0, 0), ZX_OK);
  EXPECT_EQ(request->CacheFlush(10, 10), ZX_OK);
  EXPECT_EQ(request->CacheFlush(0, ZX_PAGE_SIZE + 1), ZX_ERR_OUT_OF_RANGE);
  EXPECT_EQ(request->CacheFlush(ZX_PAGE_SIZE + 1, 0), ZX_ERR_OUT_OF_RANGE);
}

TEST(UsbRequestTest, CacheInvalidateFlush) {
  std::optional<Request> request;
  EXPECT_EQ(Request::Alloc(&request, ZX_PAGE_SIZE, 0, kParentReqSize), ZX_OK);

  EXPECT_EQ(request->CacheFlushInvalidate(0, 0), ZX_OK);
  EXPECT_EQ(request->CacheFlushInvalidate(10, 10), ZX_OK);
  EXPECT_EQ(request->CacheFlushInvalidate(0, ZX_PAGE_SIZE + 1), ZX_ERR_OUT_OF_RANGE);
  EXPECT_EQ(request->CacheFlushInvalidate(ZX_PAGE_SIZE + 1, 0), ZX_ERR_OUT_OF_RANGE);
}

TEST(UsbRequestTest, PhysMap) {
  zx_handle_t bti_handle;
  ASSERT_EQ(fake_bti_create(&bti_handle), ZX_OK, "");
  fbl::AutoCall cleanup([&]() { fake_bti_destroy(bti_handle); });
  zx::unowned_bti bti(bti_handle);

  std::optional<Request> request;
  ASSERT_EQ(Request::Alloc(&request, PAGE_SIZE * 4, 1, kParentReqSize), ZX_OK);

  ASSERT_EQ(request->PhysMap(*bti), ZX_OK);
  ASSERT_EQ(request->request()->phys_count, 4u);
}

TEST(UsbRequestTest, PhysIter) {
  zx_handle_t bti_handle;
  ASSERT_EQ(fake_bti_create(&bti_handle), ZX_OK, "");
  fbl::AutoCall cleanup([&]() { fake_bti_destroy(bti_handle); });
  zx::unowned_bti bti(bti_handle);

  std::optional<Request> request;
  ASSERT_EQ(Request::Alloc(&request, PAGE_SIZE * 4, 1, kParentReqSize), ZX_OK);

  ASSERT_EQ(request->PhysMap(*bti), ZX_OK);
  auto* req = request->take();
  for (size_t i = 0; i < req->phys_count; i++) {
    req->phys_list[i] = ZX_PAGE_SIZE * i;
  }
  request = usb::Request(req, kParentReqSize);

  size_t count = 0;
  for (auto [paddr, size] : request->phys_iter(ZX_PAGE_SIZE)) {
    EXPECT_EQ(paddr, ZX_PAGE_SIZE * count);
    EXPECT_EQ(size, ZX_PAGE_SIZE);
    ++count;
  }
  EXPECT_EQ(count, 4);
}

TEST(UsbRequestTest, SetScatterGatherList) {
  std::optional<Request> request;
  ASSERT_EQ(Request::Alloc(&request, PAGE_SIZE * 3, 1, kParentReqSize), ZX_OK);
  // Wrap around the end of the request.
  constexpr phys_iter_sg_entry_t kWrapped[] = {{.length = 10, .offset = (3 * PAGE_SIZE) - 10},
                                               {.length = 50, .offset = 0}};
  EXPECT_EQ(request->SetScatterGatherList(kWrapped, fbl::count_of(kWrapped)), ZX_OK);
  EXPECT_EQ(request->request()->header.length, 60u);

  constexpr phys_iter_sg_entry_t kUnordered[] = {{.length = 100, .offset = 2 * PAGE_SIZE},
                                                 {.length = 50, .offset = 500},
                                                 {.length = 10, .offset = 2000}};
  EXPECT_EQ(request->SetScatterGatherList(kUnordered, fbl::count_of(kUnordered)), ZX_OK);
  EXPECT_EQ(request->request()->header.length, 160u);
}

TEST(UsbRequestTest, InvalidScatterGatherList) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE * 3, 0, &vmo), ZX_OK);
  std::optional<Request> request;
  ASSERT_EQ(Request::AllocVmo(&request, vmo, PAGE_SIZE, PAGE_SIZE * 3, 0, kParentReqSize), ZX_OK);

  constexpr phys_iter_sg_entry_t kOutOfBounds[] = {
      {.length = 10, .offset = PAGE_SIZE * 3},
  };
  EXPECT_NE(request->SetScatterGatherList(kOutOfBounds, fbl::count_of(kOutOfBounds)), ZX_OK,
            "entry ends past end of vmo");

  constexpr phys_iter_sg_entry_t kEmpty[] = {
      {.length = 0, .offset = 0},
  };
  EXPECT_NE(request->SetScatterGatherList(kEmpty, fbl::count_of(kEmpty)), ZX_OK, "empty entry");
}

TEST(UsbRequestTest, ScatterGatherPhysIter) {
  zx_handle_t bti_handle;
  ASSERT_EQ(fake_bti_create(&bti_handle), ZX_OK, "");
  fbl::AutoCall cleanup([&]() { fake_bti_destroy(bti_handle); });
  zx::unowned_bti bti(bti_handle);

  std::optional<Request> request;
  ASSERT_EQ(Request::Alloc(&request, PAGE_SIZE * 4, 1, kParentReqSize), ZX_OK);

  ASSERT_EQ(request->PhysMap(*bti), ZX_OK);

  constexpr phys_iter_sg_entry_t kUnordered[] = {{.length = 100, .offset = 2 * PAGE_SIZE},
                                                 {.length = 50, .offset = 500},
                                                 {.length = 10, .offset = 2000}};
  EXPECT_EQ(request->SetScatterGatherList(kUnordered, fbl::count_of(kUnordered)), ZX_OK);

  auto* req = request->take();
  for (size_t i = 0; i < req->phys_count; i++) {
    req->phys_list[i] = ZX_PAGE_SIZE * (i * 2 + 1);
  }
  request = usb::Request(req, kParentReqSize);

  auto phys_iter = request->phys_iter(ZX_PAGE_SIZE);
  auto iter = phys_iter.begin();
  const auto end = phys_iter.end();

  {
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 5 * PAGE_SIZE);
    EXPECT_EQ(size, 100);
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, ZX_PAGE_SIZE + 500);
    EXPECT_EQ(size, 50);
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, ZX_PAGE_SIZE + 2000);
    EXPECT_EQ(size, 10);
  }

  ++iter;
  EXPECT_TRUE(iter == end);
}

TEST(UsbRequestTest, MultipleSection) {
  constexpr size_t kBaseReqSize = sizeof(usb_request_t);
  constexpr size_t kFirstLayerReqSize = Request::RequestSize(kBaseReqSize);
  constexpr size_t kSecondLayerReqSize =
      usb::BorrowedRequest<void>::RequestSize(kFirstLayerReqSize);

  std::optional<Request> request;
  ASSERT_EQ(Request::Alloc(&request, 0, 0, kSecondLayerReqSize), ZX_OK);

  usb::BorrowedRequest request2(request->take(), kNoCallback, kFirstLayerReqSize);
  usb::BorrowedRequest request3(request2.take(), kNoCallback, kBaseReqSize);
  request = usb::Request(request3.take(), kSecondLayerReqSize);
}

TEST(UsbRequestTest, PrivateStorage) {
  constexpr size_t kRequestSize = usb::Request<uint32_t>::RequestSize(kParentReqSize);
  std::optional<usb::Request<uint32_t>> request;
  EXPECT_EQ(usb::Request<uint32_t>::Alloc(&request, 0, 0, kRequestSize), ZX_OK);
  *request->private_storage() = 1001;
  ASSERT_EQ(*request->private_storage(), 1001);
}

TEST(UsbRequestTest, Callback) {
  constexpr size_t kBaseReqSize = sizeof(usb_request_t);
  constexpr size_t kFirstLayerReqSize = Request::RequestSize(kBaseReqSize);

  bool called = false;
  auto callback = [](void* ctx, usb_request_t* request) {
    *static_cast<bool*>(ctx) = true;
    // We take ownership.
    Request unused(request, kBaseReqSize);
  };
  usb_request_complete_t complete_cb = {
      .callback = callback,
      .ctx = &called,
  };

  std::optional<Request> request;
  ASSERT_EQ(Request::Alloc(&request, 0, 0, kFirstLayerReqSize), ZX_OK);

  usb::BorrowedRequest<void> request2(request->take(), complete_cb, kBaseReqSize);
  request2.Complete(ZX_OK, 0);
  EXPECT_TRUE(called);
}

TEST(UsbRequestTest, CallbackRequest) {
  usb_function_protocol_t fake_function = {};
  usb_function_protocol_ops_t fake_ops;
  fake_ops.request_queue = [](void* ctx, usb_request_t* usb_request,
                              const usb_request_complete_t* complete_cb) {
    usb_request_complete(usb_request, ZX_OK, 0, complete_cb);
  };
  fake_function.ops = &fake_ops;
  using Request = usb::CallbackRequest<sizeof(std::max_align_t)>;
  std::optional<Request> req;
  int invoked = 0;
  bool invoked_other = false;
  ddk::UsbFunctionProtocolClient client(&fake_function);
  ASSERT_EQ(Request::Alloc(&req, 0, 0, sizeof(usb_request_t),
                           [&](Request request) {
                             invoked++;
                             if (invoked == 5) {
                               Request::Queue(std::move(request), client,
                                              [&](Request request) { invoked_other = true; });
                             } else {
                               Request::Queue(std::move(request), client);
                             }
                           }),
            ZX_OK);

  Request::Queue(std::move(*req), client);
  ASSERT_EQ(5, invoked);
  ASSERT_TRUE(invoked_other);
}

}  // namespace
