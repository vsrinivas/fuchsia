// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "usb/request-cpp.h"

namespace {

using Request = usb::Request<void>;

constexpr size_t kParentReqSize = sizeof(usb_request_t);
constexpr usb_request_complete_t kNoCallback = {};

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

  usb::RequestQueue<uint64_t> queue;
  for (size_t i = 0; i < 10; i++) {
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
    ++count;
  }
  EXPECT_EQ(count, 10);
}

}  // namespace
