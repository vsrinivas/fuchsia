// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb/request-cpp.h"

#include <fuchsia/hardware/usb/function/cpp/banjo.h>
#include <lib/fake-bti/bti.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

namespace {

using Request = usb::Request<void>;

constexpr size_t kParentReqSize = sizeof(usb_request_t);
constexpr size_t kReqSize = Request::RequestSize(kParentReqSize);
constexpr usb_request_complete_t kNoCallback = {};

TEST(UsbRequestListTest, TrivialLifetime) {
  usb::RequestList<void> list;
  usb::BorrowedRequestList<void> unowned_list;
}

TEST(UsbRequestListTest, SingleRequest) {
  std::optional<Request> opt_request;
  ASSERT_EQ(Request::Alloc(&opt_request, 0, 0, kParentReqSize), ZX_OK);
  Request request = *std::move(opt_request);

  usb::RequestList<void> list;
  // Empty list.
  EXPECT_EQ(list.size(), 0u);
  EXPECT_TRUE(list.begin() == std::nullopt);

  list.push_back(&request);
  EXPECT_EQ(list.size(), 1u);

  // List only has one request.
  EXPECT_TRUE(list.prev(&request) == std::nullopt);
  EXPECT_TRUE(list.next(&request) == std::nullopt);

  std::optional<size_t> idx = list.find(&request);
  EXPECT_TRUE(idx.has_value());
  EXPECT_EQ(idx.value(), 0u);

  // Delete the request and verify it's no longer in the list.
  EXPECT_TRUE(list.erase(&request));
  EXPECT_EQ(list.size(), 0u);

  idx = list.find(&request);
  EXPECT_FALSE(idx.has_value());
}

TEST(UsbRequestListTest, MultipleRequest) {
  usb::RequestList<void> list;
  // This is for verifying prev / next pointer values when iterating the list.
  usb_request_t* raw_reqs[10];

  for (size_t i = 0; i < 10; i++) {
    std::optional<Request> opt_request;
    ASSERT_EQ(Request::Alloc(&opt_request, 0, 0, kParentReqSize), ZX_OK);
    Request request = *std::move(opt_request);

    list.push_back(&request);
    EXPECT_EQ(list.size(), i + 1);

    raw_reqs[i] = request.take();
  }
  EXPECT_EQ(list.size(), 10u);

  // Verify iterating in both directions.
  auto opt_request = list.begin();
  for (size_t i = 0; i < 10; i++) {
    EXPECT_TRUE(opt_request.has_value());
    Request request = *std::move(opt_request);

    std::optional<size_t> idx = list.find(&request);
    EXPECT_TRUE(idx.has_value());
    EXPECT_EQ(idx.value(), i);

    auto prev = list.prev(&request);
    if (i == 0) {
      EXPECT_FALSE(prev.has_value());
    } else {
      EXPECT_TRUE(prev.has_value());
      EXPECT_EQ(prev->request(), raw_reqs[i - 1]);
    }

    auto next = list.next(&request);
    if (i == 9) {
      EXPECT_FALSE(next.has_value());
    } else {
      EXPECT_TRUE(next.has_value());
      EXPECT_EQ(next->request(), raw_reqs[i + 1]);
    }

    opt_request = std::move(next);
  }
  EXPECT_FALSE(opt_request.has_value());

  for (size_t i = 0; i < 10; i++) {
    auto opt_request = list.begin();
    EXPECT_TRUE(opt_request.has_value());
    Request request = *std::move(opt_request);
    EXPECT_TRUE(list.erase(&request));

    // Force the destructor to run.
    __UNUSED auto req = Request(raw_reqs[i], kParentReqSize);
  }
  EXPECT_EQ(list.size(), 0u);
  EXPECT_FALSE(list.begin().has_value());
}

TEST(UsbRequestListTest, Move) {
  usb::RequestList<void> list1;
  usb::RequestList<void> list2;

  usb_request_t* raw_reqs[10];

  for (size_t i = 0; i < 10; i++) {
    std::optional<Request> opt_request;
    ASSERT_EQ(Request::Alloc(&opt_request, 0, 0, kParentReqSize), ZX_OK);
    Request request = *std::move(opt_request);
    list1.push_back(&request);
    raw_reqs[i] = request.take();
  }
  EXPECT_EQ(list1.size(), 10u);
  EXPECT_EQ(list2.size(), 0u);

  list2 = std::move(list1);
  EXPECT_EQ(list1.size(), 0u);
  EXPECT_EQ(list2.size(), 10u);

  size_t count = 0;
  std::optional<Request> opt_request = list2.begin();
  while (opt_request) {
    Request request = *std::move(opt_request);
    std::optional<Request> next = list2.next(&request);

    EXPECT_EQ(request.request(), raw_reqs[count]);
    EXPECT_TRUE(list2.erase(&request));

    // Force the destructor to run.
    __UNUSED auto req = Request(raw_reqs[count], kParentReqSize);

    count++;
    opt_request = std::move(next);
  }
  EXPECT_EQ(count, 10u);
  EXPECT_TRUE(list2.begin() == std::nullopt);
}

TEST(UsbRequestListTest, Release) {
  usb::RequestList<void> list;
  usb_request_t* raw_reqs[10];

  for (size_t i = 0; i < 10; i++) {
    std::optional<Request> opt_request;
    ASSERT_EQ(Request::Alloc(&opt_request, 0, 0, kParentReqSize), ZX_OK);
    Request request = *std::move(opt_request);
    list.push_back(&request);
    EXPECT_EQ(list.size(), i + 1);

    raw_reqs[i] = request.take();
  }

  list.Release();
  EXPECT_EQ(list.size(), 0u);
  EXPECT_FALSE(list.begin().has_value());

  for (size_t i = 0; i < 10; i++) {
    // Force the destructor to run.
    __UNUSED auto req = Request(raw_reqs[i], kParentReqSize);
  }
}

TEST(UsbRequestListTest, MultipleLayer) {
  using FirstLayerReq = usb::BorrowedRequest<void>;
  using SecondLayerReq = usb::Request<void>;

  constexpr size_t kBaseReqSize = sizeof(usb_request_t);
  constexpr size_t kFirstLayerReqSize = FirstLayerReq::RequestSize(kBaseReqSize);

  usb_request_t* raw_reqs[10];

  usb::RequestList<void> second_layer_list;
  for (size_t i = 0; i < 10; i++) {
    std::optional<SecondLayerReq> opt_request;
    ASSERT_EQ(SecondLayerReq::Alloc(&opt_request, 0, 0, kFirstLayerReqSize), ZX_OK);
    ASSERT_TRUE(opt_request.has_value());
    Request request = *std::move(opt_request);
    second_layer_list.push_back(&request);
    raw_reqs[i] = request.take();
  }
  EXPECT_EQ(second_layer_list.size(), 10u);

  usb::BorrowedRequestList<void> first_layer_list;
  // Add the requests also into the first layer list.
  for (size_t i = 0; i < 10; i++) {
    FirstLayerReq unowned(raw_reqs[i], kNoCallback, kBaseReqSize, /* allow_destruct */ false);
    first_layer_list.push_back(&unowned);
  }
  EXPECT_EQ(first_layer_list.size(), 10u);

  // Remove the requests from both lists.
  for (size_t i = 0; i < 10; i++) {
    FirstLayerReq unowned(raw_reqs[i], kBaseReqSize);
    std::optional<size_t> idx = first_layer_list.find(&unowned);
    EXPECT_TRUE(idx.has_value());
    EXPECT_EQ(idx.value(), 0u);
    EXPECT_TRUE(first_layer_list.erase(&unowned));

    SecondLayerReq request(unowned.take(), kFirstLayerReqSize);
    idx = second_layer_list.find(&request);
    EXPECT_TRUE(idx.has_value());
    EXPECT_EQ(idx.value(), 0u);
    EXPECT_TRUE(second_layer_list.erase(&request));
  }
  EXPECT_EQ(first_layer_list.size(), 0u);
  EXPECT_EQ(second_layer_list.size(), 0u);
}

TEST(UsbRequestListTest, MultipleLayerWithStorage) {
  using FirstLayerReq = usb::BorrowedRequest<char>;
  using SecondLayerReq = usb::Request<uint64_t>;

  constexpr size_t kBaseReqSize = sizeof(usb_request_t);
  constexpr size_t kFirstLayerReqSize = FirstLayerReq::RequestSize(kBaseReqSize);

  usb_request_t* raw_reqs[10];

  usb::RequestList<uint64_t> second_layer_list;
  for (size_t i = 0; i < 10; i++) {
    std::optional<SecondLayerReq> opt_request;
    ASSERT_EQ(SecondLayerReq::Alloc(&opt_request, 0, 0, kFirstLayerReqSize), ZX_OK);
    SecondLayerReq request = *std::move(opt_request);

    *request.private_storage() = i;
    EXPECT_EQ(*request.private_storage(), i);
    second_layer_list.push_back(&request);
    raw_reqs[i] = request.take();
  }
  EXPECT_EQ(second_layer_list.size(), 10u);

  usb::BorrowedRequestList<char> first_layer_list;
  size_t count = 0;
  // Add the requests also into the first layer list.
  for (size_t i = 0; i < 10; i++) {
    FirstLayerReq unowned(raw_reqs[i], kNoCallback, kBaseReqSize, /* allow_destruct */ false);
    *unowned.private_storage() = static_cast<char>('a' + first_layer_list.size());
    first_layer_list.push_back(&unowned);
  }
  EXPECT_EQ(first_layer_list.size(), 10u);

  // Verify the first layer list node's private storage and also erase them along the way.
  count = 0;
  auto opt_unowned = first_layer_list.begin();
  while (opt_unowned) {
    auto unowned = *std::move(opt_unowned);
    auto next = first_layer_list.next(&unowned);

    EXPECT_EQ(*unowned.private_storage(), static_cast<char>('a' + count));
    EXPECT_TRUE(first_layer_list.erase(&unowned));

    ++count;
    opt_unowned = std::move(next);
  }
  EXPECT_EQ(count, 10);
  EXPECT_EQ(first_layer_list.size(), 0u);

  // Verify the second layer list node's private storage and also erase them along the way.
  count = 0;
  auto opt_request = second_layer_list.begin();
  while (opt_request) {
    auto request = *std::move(opt_request);
    auto next = second_layer_list.next(&request);

    EXPECT_EQ(*request.private_storage(), count);
    EXPECT_TRUE(second_layer_list.erase(&request));

    ++count;
    opt_request = std::move(next);
  }
  EXPECT_EQ(count, 10);
  EXPECT_EQ(second_layer_list.size(), 0u);

  for (size_t i = 0; i < 10; i++) {
    // Force the destructor to run.
    __UNUSED auto req = SecondLayerReq(raw_reqs[i], kFirstLayerReqSize);
  }
}

TEST(UsbRequestListTest, MultipleLayerWithCallback) {
  using FirstLayerReq = usb::BorrowedRequest<char>;
  using SecondLayerReq = usb::Request<uint64_t>;

  constexpr size_t kBaseReqSize = sizeof(usb_request_t);
  constexpr size_t kFirstLayerReqSize = FirstLayerReq::RequestSize(kBaseReqSize);

  usb_request_t* raw_reqs[10];

  usb::RequestList<uint64_t> second_layer_list;
  for (size_t i = 0; i < 10; i++) {
    std::optional<SecondLayerReq> opt_request;
    ASSERT_EQ(SecondLayerReq::Alloc(&opt_request, 0, 0, kFirstLayerReqSize), ZX_OK);
    SecondLayerReq request = *std::move(opt_request);

    *request.private_storage() = i;
    EXPECT_EQ(*request.private_storage(), i);
    second_layer_list.push_back(&request);

    raw_reqs[i] = request.take();
  }
  EXPECT_EQ(second_layer_list.size(), 10u);

  std::atomic<size_t> num_callbacks{0};

  auto callback = [](void* ctx, usb_request_t* request) {
    auto counter = static_cast<std::atomic<size_t>*>(ctx);
    ++(*counter);
  };

  usb_request_complete_t complete_cb = {
      .callback = callback,
      .ctx = &num_callbacks,
  };

  {
    usb::BorrowedRequestList<char> first_layer_list;

    // Store the requests into the first layer list.
    for (size_t i = 0; i < 10; i++) {
      FirstLayerReq unowned(raw_reqs[i], complete_cb, kBaseReqSize,
                            /* allow_destruct */ false);
      first_layer_list.push_back(&unowned);
    }
    EXPECT_EQ(first_layer_list.size(), 10u);
    EXPECT_EQ(second_layer_list.size(), 10u);
  }
  // The first layer list destruction should not trigger any callbacks.
  EXPECT_EQ(num_callbacks.load(), 0u);

  // Verify the second layer list node's private storage and also erase them along the way.
  size_t count = 0;
  auto opt_request = second_layer_list.begin();
  while (opt_request) {
    auto request = *std::move(opt_request);
    auto next = second_layer_list.next(&request);

    EXPECT_EQ(*request.private_storage(), count);
    EXPECT_TRUE(second_layer_list.erase(&request));

    ++count;
    opt_request = std::move(next);
  }
  EXPECT_EQ(count, 10);
  EXPECT_EQ(second_layer_list.size(), 0u);

  for (size_t i = 0; i < 10; i++) {
    // Force the destructor to run.
    __UNUSED auto req = SecondLayerReq(raw_reqs[i], kFirstLayerReqSize);
  }
}

TEST(UsbRequestPoolTest, TrivialLifetime) { usb::RequestPool pool; }

TEST(UsbRequestPoolTest, SingleRequest) {
  std::optional<Request> request;
  ASSERT_EQ(Request::Alloc(&request, 0, 0, kParentReqSize), ZX_OK);

  usb::RequestPool pool;
  EXPECT_TRUE(pool.Get(kReqSize) == std::nullopt);
  pool.Add(*std::move(request));
  EXPECT_TRUE(pool.Get(kReqSize + 1) == std::nullopt);
  EXPECT_TRUE(pool.Get(kReqSize) != std::nullopt);
  EXPECT_TRUE(pool.Get(kReqSize) == std::nullopt);
}

TEST(UsbRequestPoolTest, MultipleRequest) {
  usb::RequestPool pool;

  for (size_t i = 0; i < 10; i++) {
    std::optional<Request> request;
    ASSERT_EQ(Request::Alloc(&request, 0, 0, kParentReqSize), ZX_OK);
    pool.Add(*std::move(request));
  }

  for (size_t i = 0; i < 10; i++) {
    EXPECT_TRUE(pool.Get(kReqSize) != std::nullopt);
  }
  EXPECT_TRUE(pool.Get(kReqSize) == std::nullopt);
}

TEST(UsbRequestPoolTest, MultipleSize) {
  usb::RequestPool pool;

  for (size_t i = 0; i < 10; i++) {
    const size_t size = kParentReqSize + i * 8;
    std::optional<Request> request;
    ASSERT_EQ(Request::Alloc(&request, 0, 0, size), ZX_OK);
    pool.Add(*std::move(request));
  }

  for (size_t i = 0; i < 10; i++) {
    const size_t size = Request::RequestSize(kParentReqSize + i * 8);
    EXPECT_TRUE(pool.Get(size) != std::nullopt);
    EXPECT_TRUE(pool.Get(size) == std::nullopt);
  }
}

TEST(UsbRequestPoolTest, Release) {
  usb::RequestPool pool;

  for (size_t i = 0; i < 10; i++) {
    std::optional<Request> request;
    ASSERT_EQ(Request::Alloc(&request, 0, 0, kParentReqSize), ZX_OK);
    pool.Add(*std::move(request));
  }

  pool.Release();
  EXPECT_TRUE(pool.Get(kReqSize) == std::nullopt);
}

TEST(UsbRequestQueue, TrivialLifetime) {
  usb::RequestQueue<void> queue;
  usb::BorrowedRequestQueue<void> unowned_queue;
}

TEST(UsbRequestQueue, SingleRequest) {
  std::optional<Request> request;
  ASSERT_EQ(Request::Alloc(&request, 0, 0, kParentReqSize), ZX_OK);

  usb::RequestQueue<void> queue;
  EXPECT_TRUE(queue.pop() == std::nullopt);
  queue.push(*std::move(request));
  EXPECT_TRUE(queue.pop() != std::nullopt);
  EXPECT_TRUE(queue.pop() == std::nullopt);
}

TEST(UsbRequestQueue, MultipleRequest) {
  usb::RequestQueue<void> queue;

  for (size_t i = 0; i < 10; i++) {
    std::optional<Request> request;
    ASSERT_EQ(Request::Alloc(&request, 0, 0, kParentReqSize), ZX_OK);
    queue.push(*std::move(request));
  }

  for (size_t i = 0; i < 10; i++) {
    EXPECT_TRUE(queue.pop() != std::nullopt);
  }
  EXPECT_TRUE(queue.pop() == std::nullopt);
}

TEST(UsbRequestQueue, Move) {
  usb::RequestQueue<void> queue1;
  usb::RequestQueue<void> queue2;

  for (size_t i = 0; i < 10; i++) {
    std::optional<Request> request;
    ASSERT_EQ(Request::Alloc(&request, 0, 0, kParentReqSize), ZX_OK);
    queue1.push(*std::move(request));
  }

  queue2 = std::move(queue1);
  EXPECT_TRUE(queue1.pop() == std::nullopt);

  for (size_t i = 0; i < 10; i++) {
    EXPECT_TRUE(queue2.pop() != std::nullopt);
  }
  EXPECT_TRUE(queue2.pop() == std::nullopt);
}

TEST(UsbRequestQueue, Release) {
  usb::RequestQueue<void> queue;

  for (size_t i = 0; i < 10; i++) {
    std::optional<Request> request;
    ASSERT_EQ(Request::Alloc(&request, 0, 0, kParentReqSize), ZX_OK);
    queue.push(*std::move(request));
  }

  queue.Release();
  EXPECT_TRUE(queue.pop() == std::nullopt);
}

TEST(UsbRequestQueue, MultipleLayer) {
  using FirstLayerReq = usb::BorrowedRequest<void>;
  using SecondLayerReq = usb::Request<void>;

  constexpr size_t kBaseReqSize = sizeof(usb_request_t);
  constexpr size_t kFirstLayerReqSize = FirstLayerReq::RequestSize(kBaseReqSize);

  usb::RequestQueue<void> queue;
  for (size_t i = 0; i < 10; i++) {
    std::optional<SecondLayerReq> request;
    ASSERT_EQ(SecondLayerReq::Alloc(&request, 0, 0, kFirstLayerReqSize), ZX_OK);
    queue.push(*std::move(request));
  }

  usb::BorrowedRequestQueue<void> queue2;
  size_t count = 0;
  for (auto request = queue.pop(); request; request = queue.pop()) {
    FirstLayerReq unowned(request->take(), kNoCallback, kBaseReqSize);
    queue2.push(std::move(unowned));
    ++count;
  }
  EXPECT_EQ(count, 10);

  count = 0;
  for (auto unowned = queue2.pop(); unowned; unowned = queue2.pop()) {
    SecondLayerReq request(unowned->take(), kFirstLayerReqSize);
    queue.push(std::move(request));
    ++count;
  }
  EXPECT_EQ(count, 10);
}

TEST(UsbRequestQueue, MultipleLayerWithStorage) {
  using FirstLayerReq = usb::BorrowedRequest<char>;
  using SecondLayerReq = usb::Request<uint64_t>;

  constexpr size_t kBaseReqSize = sizeof(usb_request_t);
  constexpr size_t kFirstLayerReqSize = FirstLayerReq::RequestSize(kBaseReqSize);

  usb::RequestQueue<uint64_t> queue;
  for (size_t i = 0; i < 10; i++) {
    std::optional<SecondLayerReq> request;
    ASSERT_EQ(SecondLayerReq::Alloc(&request, 0, 0, kFirstLayerReqSize), ZX_OK);
    *request->private_storage() = i;
    EXPECT_EQ(*request->private_storage(), i);
    queue.push(*std::move(request));
  }

  usb::BorrowedRequestQueue<char> queue2;
  size_t count = 0;
  for (auto request = queue.pop(); request; request = queue.pop()) {
    FirstLayerReq unowned(request->take(), kNoCallback, kBaseReqSize);
    *unowned.private_storage() = static_cast<char>('a' + count);
    queue2.push(std::move(unowned));
    ++count;
  }
  EXPECT_EQ(count, 10);

  count = 0;
  for (auto unowned = queue2.pop(); unowned; unowned = queue2.pop()) {
    EXPECT_EQ(*unowned->private_storage(), static_cast<char>('a' + count));
    SecondLayerReq request(unowned->take(), kFirstLayerReqSize);
    EXPECT_EQ(*request.private_storage(), count);
    queue.push(std::move(request));
    ++count;
  }
  EXPECT_EQ(count, 10);
}

TEST(UsbRequestQueue, MultipleLayerWithCallback) {
  using FirstLayerReq = usb::BorrowedRequest<char>;
  using SecondLayerReq = usb::Request<uint64_t>;

  constexpr size_t kBaseReqSize = sizeof(usb_request_t);
  constexpr size_t kFirstLayerReqSize = FirstLayerReq::RequestSize(kBaseReqSize);
  constexpr int iter_count = 10;

  usb::RequestQueue<uint64_t> queue;
  for (size_t i = 0; i < iter_count; i++) {
    std::optional<SecondLayerReq> request;
    ASSERT_EQ(SecondLayerReq::Alloc(&request, 0, 0, kFirstLayerReqSize), ZX_OK);
    *request->private_storage() = i;
    EXPECT_EQ(*request->private_storage(), i);
    queue.push(*std::move(request));
  }

  auto callback = [](void* ctx, usb_request_t* request) {
    auto* queue = static_cast<usb::RequestQueue<uint64_t>*>(ctx);
    queue->push(SecondLayerReq(request, kFirstLayerReqSize));
  };
  usb_request_complete_t complete_cb = {
      .callback = callback,
      .ctx = &queue,
  };

  usb::BorrowedRequestQueue<char> queue2;
  for (auto request = queue.pop(); request; request = queue.pop()) {
    FirstLayerReq unowned(request->take(), complete_cb, kBaseReqSize);
    queue2.push(std::move(unowned));
  }
  queue2.CompleteAll(ZX_OK, 0);

  size_t count = 0;
  for (auto request = queue.pop(); request; request = queue.pop()) {
    EXPECT_EQ(*request->private_storage(), count);
    request->Release();  // Copy elision.
    ++count;
  }
  EXPECT_EQ(count, iter_count);
}

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
  free(request->take());
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
  zx::bti bti;
  ASSERT_EQ(fake_bti_create(bti.reset_and_get_address()), ZX_OK, "");

  std::optional<Request> request;
  ASSERT_EQ(Request::Alloc(&request, PAGE_SIZE * 4, 1, kParentReqSize), ZX_OK);

  ASSERT_EQ(request->PhysMap(bti), ZX_OK);
  ASSERT_EQ(request->request()->phys_count, 4u);
}

TEST(UsbRequestTest, PhysIter) {
  zx::bti bti;
  ASSERT_EQ(fake_bti_create(bti.reset_and_get_address()), ZX_OK, "");

  std::optional<Request> request;
  ASSERT_EQ(Request::Alloc(&request, PAGE_SIZE * 4, 1, kParentReqSize), ZX_OK);

  ASSERT_EQ(request->PhysMap(bti), ZX_OK);
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
  EXPECT_EQ(request->SetScatterGatherList(kWrapped, std::size(kWrapped)), ZX_OK);
  EXPECT_EQ(request->request()->header.length, 60u);

  constexpr phys_iter_sg_entry_t kUnordered[] = {{.length = 100, .offset = 2 * PAGE_SIZE},
                                                 {.length = 50, .offset = 500},
                                                 {.length = 10, .offset = 2000}};
  EXPECT_EQ(request->SetScatterGatherList(kUnordered, std::size(kUnordered)), ZX_OK);
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
  EXPECT_NE(request->SetScatterGatherList(kOutOfBounds, std::size(kOutOfBounds)), ZX_OK,
            "entry ends past end of vmo");

  constexpr phys_iter_sg_entry_t kEmpty[] = {
      {.length = 0, .offset = 0},
  };
  EXPECT_NE(request->SetScatterGatherList(kEmpty, std::size(kEmpty)), ZX_OK, "empty entry");
}

TEST(UsbRequestTest, ScatterGatherPhysIter) {
  zx::bti bti;
  ASSERT_EQ(fake_bti_create(bti.reset_and_get_address()), ZX_OK, "");

  std::optional<Request> request;
  ASSERT_EQ(Request::Alloc(&request, PAGE_SIZE * 4, 1, kParentReqSize), ZX_OK);

  ASSERT_EQ(request->PhysMap(bti), ZX_OK);

  constexpr phys_iter_sg_entry_t kUnordered[] = {{.length = 100, .offset = 2 * PAGE_SIZE},
                                                 {.length = 50, .offset = 500},
                                                 {.length = 10, .offset = 2000}};
  EXPECT_EQ(request->SetScatterGatherList(kUnordered, std::size(kUnordered)), ZX_OK);

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
