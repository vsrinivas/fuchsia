// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb/request-cpp.h"

#include <unittest/unittest.h>

namespace {

using Request = usb::Request<void>;

constexpr size_t kParentReqSize = sizeof(usb_request_t);
constexpr size_t kReqSize = Request::RequestSize(kParentReqSize);
constexpr usb_request_complete_t kNoCallback = {};

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

bool MoveTest() {
    BEGIN_TEST;
    usb::RequestQueue<void> queue1;
    usb::RequestQueue<void> queue2;

    for (size_t i = 0; i < 10; i++) {
        std::optional<Request> request;
        ASSERT_EQ(Request::Alloc(&request, 0, 0, kReqSize),
                  ZX_OK);
        queue1.push(std::move(*request));
    }

    queue2 = std::move(queue1);
    EXPECT_TRUE(queue1.pop() == std::nullopt);

    for (size_t i = 0; i < 10; i++) {
        EXPECT_TRUE(queue2.pop() != std::nullopt);
    }
    EXPECT_TRUE(queue2.pop() == std::nullopt);
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

    queue.Release();
    EXPECT_TRUE(queue.pop() == std::nullopt);
    END_TEST;
}

bool MultipleLayerTest() {
    BEGIN_TEST;

    using FirstLayerReq = usb::UnownedRequest<void>;
    using SecondLayerReq = usb::Request<void>;

    constexpr size_t kBaseReqSize = sizeof(usb_request_t);
    constexpr size_t kFirstLayerReqSize = FirstLayerReq::RequestSize(kBaseReqSize);
    constexpr size_t kSecondLayerReqSize = SecondLayerReq::RequestSize(kFirstLayerReqSize);

    usb::RequestQueue<void> queue;
    for (size_t i = 0; i < 10; i++) {
        std::optional<SecondLayerReq> request;
        ASSERT_EQ(SecondLayerReq::Alloc(&request, 0, 0, kSecondLayerReqSize, kFirstLayerReqSize),
                  ZX_OK);
        queue.push(std::move(*request));
    }

    usb::UnownedRequestQueue<void> queue2;
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

    END_TEST;
}

bool MultipleLayerWithStorageTest() {
    BEGIN_TEST;

    using FirstLayerReq = usb::UnownedRequest<char>;
    using SecondLayerReq = usb::Request<uint64_t>;

    constexpr size_t kBaseReqSize = sizeof(usb_request_t);
    constexpr size_t kFirstLayerReqSize = FirstLayerReq::RequestSize(kBaseReqSize);
    constexpr size_t kSecondLayerReqSize = SecondLayerReq::RequestSize(kFirstLayerReqSize);

    usb::RequestQueue<uint64_t> queue;
    for (size_t i = 0; i < 10; i++) {
        std::optional<SecondLayerReq> request;
        ASSERT_EQ(SecondLayerReq::Alloc(&request, 0, 0, kSecondLayerReqSize, kFirstLayerReqSize),
                  ZX_OK);
        *request->private_storage() = i;
        EXPECT_EQ(*request->private_storage(), i);
        queue.push(std::move(*request));
    }

    usb::UnownedRequestQueue<char> queue2;
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

    END_TEST;
}

bool MultipleLayerWithCallbackTest() {
    BEGIN_TEST;

    using FirstLayerReq = usb::UnownedRequest<char>;
    using SecondLayerReq = usb::Request<uint64_t>;

    constexpr size_t kBaseReqSize = sizeof(usb_request_t);
    constexpr size_t kFirstLayerReqSize = FirstLayerReq::RequestSize(kBaseReqSize);
    constexpr size_t kSecondLayerReqSize = SecondLayerReq::RequestSize(kFirstLayerReqSize);

    usb::RequestQueue<uint64_t> queue;
    for (size_t i = 0; i < 10; i++) {
        std::optional<SecondLayerReq> request;
        ASSERT_EQ(SecondLayerReq::Alloc(&request, 0, 0, kSecondLayerReqSize, kFirstLayerReqSize),
                  ZX_OK);
        *request->private_storage() = i;
        EXPECT_EQ(*request->private_storage(), i);
        queue.push(std::move(*request));
    }

    auto callback = [](void* ctx, usb_request_t* request) {
        auto* queue = static_cast<usb::RequestQueue<uint64_t>*>(ctx);
        queue->push(SecondLayerReq(request, kFirstLayerReqSize));
    };
    usb_request_complete_t complete_cb = {
        .callback = callback,
        .ctx = &queue,
    };

    {
        usb::UnownedRequestQueue<char> queue2;
        for (auto request = queue.pop(); request; request = queue.pop()) {
            FirstLayerReq unowned(request->take(), complete_cb, kBaseReqSize);
            queue2.push(std::move(unowned));
        }
    }

    size_t count = 0;
    for (auto request = queue.pop(); request; request = queue.pop()) {
        EXPECT_EQ(*request->private_storage(), count);
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
RUN_TEST_SMALL(MoveTest)
RUN_TEST_SMALL(ReleaseTest)
RUN_TEST_SMALL(MultipleLayerTest)
RUN_TEST_SMALL(MultipleLayerWithStorageTest)
RUN_TEST_SMALL(MultipleLayerWithCallbackTest)
END_TEST_CASE(UsbRequestQueueTests);
