// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb/request-cpp.h"

#include <unittest/unittest.h>

namespace {

constexpr size_t kParentReqSize = sizeof(usb_request_t);
constexpr size_t kReqSize = usb::Request::RequestSize(kParentReqSize);

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    usb::RequestQueue queue;
    END_TEST;
}

bool SingleRequestTest() {
    BEGIN_TEST;
    std::optional<usb::Request> request;
    ASSERT_EQ(usb::Request::Alloc(&request, 0, 0, kReqSize),
              ZX_OK);

    usb::RequestQueue queue;
    EXPECT_TRUE(queue.pop() == std::nullopt);
    queue.push(std::move(*request));
    EXPECT_TRUE(queue.pop() != std::nullopt);
    EXPECT_TRUE(queue.pop() == std::nullopt);
    END_TEST;
}

bool MultipleRequestTest() {
    BEGIN_TEST;
    usb::RequestQueue queue;

    for (size_t i = 0; i < 10; i++) {
        std::optional<usb::Request> request;
        ASSERT_EQ(usb::Request::Alloc(&request, 0, 0, kReqSize),
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
    usb::RequestQueue queue;

    for (size_t i = 0; i < 10; i++) {
        std::optional<usb::Request> request;
        ASSERT_EQ(usb::Request::Alloc(&request, 0, 0, kReqSize),
                  ZX_OK);
        queue.push(std::move(*request));
    }

    queue.Release();
    EXPECT_TRUE(queue.pop() == std::nullopt);
    END_TEST;
}

bool MultipleLayerTest() {
    BEGIN_TEST;

    constexpr size_t kBaseReqSize = sizeof(usb_request_t);
    constexpr size_t kFirstLayerReqSize = usb::Request::RequestSize(kBaseReqSize);
    constexpr size_t kSecondLayerReqSize = usb::Request::RequestSize(kFirstLayerReqSize);

    usb::RequestQueue queue;
    for (size_t i = 0; i < 10; i++) {
        std::optional<usb::Request> request;
        ASSERT_EQ(usb::Request::Alloc(&request, 0, 0, kSecondLayerReqSize, kBaseReqSize),
                  ZX_OK);
        queue.push(std::move(*request));
    }

    usb::UnownedRequestQueue queue2;
    size_t count = 0;
    for (auto request = queue.pop(); request; request = queue.pop()) {
        usb::UnownedRequest unowned(request->release(), kFirstLayerReqSize);
        queue2.push(std::move(unowned));
        ++count;
    }
    EXPECT_EQ(count, 10);

    count = 0;
    for (auto unowned = queue2.pop(); unowned; unowned = queue2.pop()) {
        usb::Request request(unowned->release(), kFirstLayerReqSize);
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
END_TEST_CASE(UsbRequestQueueTests);
