// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb/request-cpp.h"

#include <unittest/unittest.h>

namespace {

using Request = usb::Request<void>;

constexpr size_t kParentReqSize = sizeof(usb_request_t);
constexpr size_t kReqSize = Request::RequestSize(kParentReqSize);

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    usb::RequestQueue<void> queue;
    usb::UnownedRequestQueue<void> unowned_queue;
    END_TEST;
}

bool SingleRequestTest() {
    BEGIN_TEST;
    std::optional<Request> request;
    ASSERT_EQ(Request::Alloc(&request, 0, 0, kReqSize),
              ZX_OK);

    usb::RequestQueue<void> queue;
    EXPECT_TRUE(queue.pop() == std::nullopt);
    queue.push(std::move(*request));
    EXPECT_TRUE(queue.pop() != std::nullopt);
    EXPECT_TRUE(queue.pop() == std::nullopt);
    END_TEST;
}

bool MultipleRequestTest() {
    BEGIN_TEST;
    usb::RequestQueue<void> queue;

    for (size_t i = 0; i < 10; i++) {
        std::optional<Request> request;
        ASSERT_EQ(Request::Alloc(&request, 0, 0, kReqSize),
                  ZX_OK);
        queue.push(std::move(*request));
    }

    for (size_t i = 0; i < 10; i++) {
        EXPECT_TRUE(queue.pop() != std::nullopt);
    }
    EXPECT_TRUE(queue.pop() == std::nullopt);
    END_TEST;
}

bool ReleaseTest() {
    BEGIN_TEST;
    usb::RequestQueue<void> queue;

    for (size_t i = 0; i < 10; i++) {
        std::optional<Request> request;
        ASSERT_EQ(Request::Alloc(&request, 0, 0, kReqSize),
                  ZX_OK);
        queue.push(std::move(*request));
    }

    queue.release();
    EXPECT_TRUE(queue.pop() == std::nullopt);
    END_TEST;
}

bool MultipleLayerTest() {
    BEGIN_TEST;

    constexpr size_t kBaseReqSize = sizeof(usb_request_t);
    constexpr size_t kFirstLayerReqSize = Request::RequestSize(kBaseReqSize);
    constexpr size_t kSecondLayerReqSize = Request::RequestSize(kFirstLayerReqSize);

    usb::RequestQueue<void> queue;
    for (size_t i = 0; i < 10; i++) {
        std::optional<Request> request;
        ASSERT_EQ(Request::Alloc(&request, 0, 0, kSecondLayerReqSize, kBaseReqSize),
                  ZX_OK);
        queue.push(std::move(*request));
    }

    usb::UnownedRequestQueue<void> queue2;
    size_t count = 0;
    for (auto request = queue.pop(); request; request = queue.pop()) {
        usb::UnownedRequest unowned(request->take(), kFirstLayerReqSize);
        queue2.push(std::move(unowned));
        ++count;
    }
    EXPECT_EQ(count, 10);

    count = 0;
    for (auto unowned = queue2.pop(); unowned; unowned = queue2.pop()) {
        Request request(unowned->take(), kFirstLayerReqSize);
        queue.push(std::move(request));
        ++count;
    }
    EXPECT_EQ(count, 10);

    END_TEST;
}

bool MultipleLayerWithStorageTest() {
    BEGIN_TEST;

    using FirstLayerReq = usb::Request<uint64_t>;
    using SecondLayerReq = usb::UnownedRequest<char>;

    constexpr size_t kBaseReqSize = sizeof(usb_request_t);
    constexpr size_t kFirstLayerReqSize = FirstLayerReq::RequestSize(kBaseReqSize);
    constexpr size_t kSecondLayerReqSize = SecondLayerReq::RequestSize(kFirstLayerReqSize);

    usb::RequestQueue<uint64_t> queue;
    for (size_t i = 0; i < 10; i++) {
        std::optional<FirstLayerReq> request;
        ASSERT_EQ(FirstLayerReq::Alloc(&request, 0, 0, kSecondLayerReqSize, kBaseReqSize),
                  ZX_OK);
        *request->private_storage() = i;
        EXPECT_EQ(*request->private_storage(), i);
        queue.push(std::move(*request));
    }

    usb::UnownedRequestQueue<char> queue2;
    size_t count = 0;
    for (auto request = queue.pop(); request; request = queue.pop()) {
        SecondLayerReq unowned(request->take(), kFirstLayerReqSize);
        *unowned.private_storage() = static_cast<char>('a' + count);
        queue2.push(std::move(unowned));
        ++count;
    }
    EXPECT_EQ(count, 10);

    count = 0;
    for (auto unowned = queue2.pop(); unowned; unowned = queue2.pop()) {
        EXPECT_EQ(*unowned->private_storage(), static_cast<char>('a' + count));
        FirstLayerReq request(unowned->take(), kBaseReqSize);
        EXPECT_EQ(*request.private_storage(), count);
        queue.push(std::move(request));
        ++count;
    }
    EXPECT_EQ(count, 10);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(UsbRequestQueueTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(SingleRequestTest)
RUN_TEST_SMALL(MultipleRequestTest)
RUN_TEST_SMALL(ReleaseTest)
RUN_TEST_SMALL(MultipleLayerTest)
RUN_TEST_SMALL(MultipleLayerWithStorageTest)
END_TEST_CASE(UsbRequestQueueTests);
