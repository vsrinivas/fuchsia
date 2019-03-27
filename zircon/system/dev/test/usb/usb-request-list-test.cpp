// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb/request-cpp.h"

#include <unittest/unittest.h>

namespace {

using Request = usb::Request<void>;

constexpr size_t kParentReqSize = sizeof(usb_request_t);
constexpr usb_request_complete_t kNoCallback = {};

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    usb::RequestList<void> list;
    usb::UnownedRequestList<void> unowned_list;
    END_TEST;
}

bool SingleRequestTest() {
    BEGIN_TEST;
    std::optional<Request> opt_request;
    ASSERT_EQ(Request::Alloc(&opt_request, 0, 0, kParentReqSize),
              ZX_OK);
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
    END_TEST;
}

bool MultipleRequestTest() {
    BEGIN_TEST;
    usb::RequestList<void> list;
    // This is for verifying prev / next pointer values when iterating the list.
    usb_request_t* raw_reqs[10];

    for (size_t i = 0; i < 10; i++) {
        std::optional<Request> opt_request;
        ASSERT_EQ(Request::Alloc(&opt_request, 0, 0, kParentReqSize),
                  ZX_OK);
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
    END_TEST;
}

bool MoveTest() {
    BEGIN_TEST;
    usb::RequestList<void> list1;
    usb::RequestList<void> list2;

    usb_request_t* raw_reqs[10];

    for (size_t i = 0; i < 10; i++) {
        std::optional<Request> opt_request;
        ASSERT_EQ(Request::Alloc(&opt_request, 0, 0, kParentReqSize),
                  ZX_OK);
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
    END_TEST;
}

bool ReleaseTest() {
    BEGIN_TEST;
    usb::RequestList<void> list;
    usb_request_t* raw_reqs[10];

    for (size_t i = 0; i < 10; i++) {
        std::optional<Request> opt_request;
        ASSERT_EQ(Request::Alloc(&opt_request, 0, 0, kParentReqSize),
                  ZX_OK);
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

    END_TEST;
}

bool MultipleLayerTest() {
    BEGIN_TEST;

    using FirstLayerReq = usb::UnownedRequest<void>;
    using SecondLayerReq = usb::Request<void>;

    constexpr size_t kBaseReqSize = sizeof(usb_request_t);
    constexpr size_t kFirstLayerReqSize = FirstLayerReq::RequestSize(kBaseReqSize);

    usb_request_t* raw_reqs[10];

    usb::RequestList<void> second_layer_list;
    for (size_t i = 0; i < 10; i++) {
        std::optional<SecondLayerReq> opt_request;
        ASSERT_EQ(SecondLayerReq::Alloc(&opt_request, 0, 0, kFirstLayerReqSize),
                  ZX_OK);
        ASSERT_TRUE(opt_request.has_value());
        Request request = *std::move(opt_request);
        second_layer_list.push_back(&request);
        raw_reqs[i] = request.take();
    }
    EXPECT_EQ(second_layer_list.size(), 10u);

    usb::UnownedRequestList<void> first_layer_list;
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

    END_TEST;
}

bool MultipleLayerWithStorageTest() {
    BEGIN_TEST;

    using FirstLayerReq = usb::UnownedRequest<char>;
    using SecondLayerReq = usb::Request<uint64_t>;

    constexpr size_t kBaseReqSize = sizeof(usb_request_t);
    constexpr size_t kFirstLayerReqSize = FirstLayerReq::RequestSize(kBaseReqSize);

    usb_request_t* raw_reqs[10];

    usb::RequestList<uint64_t> second_layer_list;
    for (size_t i = 0; i < 10; i++) {
        std::optional<SecondLayerReq> opt_request;
        ASSERT_EQ(SecondLayerReq::Alloc(&opt_request, 0, 0, kFirstLayerReqSize),
                  ZX_OK);
        SecondLayerReq request = *std::move(opt_request);

        *request.private_storage() = i;
        EXPECT_EQ(*request.private_storage(), i);
        second_layer_list.push_back(&request);
        raw_reqs[i] = request.take();
    }
    EXPECT_EQ(second_layer_list.size(), 10u);

    usb::UnownedRequestList<char> first_layer_list;
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

    END_TEST;
}

bool MultipleLayerWithCallbackTest() {
    BEGIN_TEST;

    using FirstLayerReq = usb::UnownedRequest<char>;
    using SecondLayerReq = usb::Request<uint64_t>;

    constexpr size_t kBaseReqSize = sizeof(usb_request_t);
    constexpr size_t kFirstLayerReqSize = FirstLayerReq::RequestSize(kBaseReqSize);

    usb_request_t* raw_reqs[10];

    usb::RequestList<uint64_t> second_layer_list;
    for (size_t i = 0; i < 10; i++) {
        std::optional<SecondLayerReq> opt_request;
        ASSERT_EQ(SecondLayerReq::Alloc(&opt_request, 0, 0, kFirstLayerReqSize),
                  ZX_OK);
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
        usb::UnownedRequestList<char> first_layer_list;

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

    END_TEST;
}
} // namespace

BEGIN_TEST_CASE(UsbRequestListTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(SingleRequestTest)
RUN_TEST_SMALL(MultipleRequestTest)
RUN_TEST_SMALL(MoveTest)
RUN_TEST_SMALL(ReleaseTest)
RUN_TEST_SMALL(MultipleLayerTest)
RUN_TEST_SMALL(MultipleLayerWithStorageTest)
RUN_TEST_SMALL(MultipleLayerWithCallbackTest)
END_TEST_CASE(UsbRequestListTests)
