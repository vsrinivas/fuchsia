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
    usb::RequestPool pool;
    END_TEST;
}

bool SingleRequestTest() {
    BEGIN_TEST;
    std::optional<Request> request;
    ASSERT_EQ(Request::Alloc(&request, 0, 0, kParentReqSize),
              ZX_OK);

    usb::RequestPool pool;
    EXPECT_TRUE(pool.Get(kReqSize) == std::nullopt);
    pool.Add(*std::move(request));
    EXPECT_TRUE(pool.Get(kReqSize + 1) == std::nullopt);
    EXPECT_TRUE(pool.Get(kReqSize) != std::nullopt);
    EXPECT_TRUE(pool.Get(kReqSize) == std::nullopt);
    END_TEST;
}

bool MultipleRequestTest() {
    BEGIN_TEST;
    usb::RequestPool pool;

    for (size_t i = 0; i < 10; i++) {
        std::optional<Request> request;
        ASSERT_EQ(Request::Alloc(&request, 0, 0, kParentReqSize),
                  ZX_OK);
        pool.Add(*std::move(request));
    }

    for (size_t i = 0; i < 10; i++) {
        EXPECT_TRUE(pool.Get(kReqSize) != std::nullopt);
    }
    EXPECT_TRUE(pool.Get(kReqSize) == std::nullopt);
    END_TEST;
}

bool MultipleSizeTest() {
    BEGIN_TEST;
    usb::RequestPool pool;

    for (size_t i = 0; i < 10; i++) {
        const size_t size = kParentReqSize + i * 8;
        std::optional<Request> request;
        ASSERT_EQ(Request::Alloc(&request, 0, 0, size),
                  ZX_OK);
        pool.Add(*std::move(request));
    }

    for (size_t i = 0; i < 10; i++) {
        const size_t size = Request::RequestSize(kParentReqSize + i * 8);
        EXPECT_TRUE(pool.Get(size) != std::nullopt);
        EXPECT_TRUE(pool.Get(size) == std::nullopt);
    }
    END_TEST;
}

bool ReleaseTest() {
    BEGIN_TEST;
    usb::RequestPool pool;

    for (size_t i = 0; i < 10; i++) {
        std::optional<Request> request;
        ASSERT_EQ(Request::Alloc(&request, 0, 0, kParentReqSize),
                  ZX_OK);
        pool.Add(*std::move(request));
    }

    pool.Release();
    EXPECT_TRUE(pool.Get(kReqSize) == std::nullopt);
    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(UsbRequestPoolTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(SingleRequestTest)
RUN_TEST_SMALL(MultipleRequestTest)
RUN_TEST_SMALL(MultipleSizeTest)
RUN_TEST_SMALL(ReleaseTest)
END_TEST_CASE(UsbRequestPoolTests)
