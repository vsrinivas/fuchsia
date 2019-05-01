// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <fidl/test/llcpp/basictypes/c/fidl.h>
#include <lib/async-loop/loop.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/time.h>
#include <memory>
#include <string.h>
#include <unittest/unittest.h>
#include <utility>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

// Interface under test
#include "generated/fidl_llcpp_basictypes.h"

// test utility functions
namespace {

bool IsPeerValid(const zx::unowned_eventpair& handle) {
    zx_signals_t observed_signals = {};
    switch (handle->wait_one(ZX_EVENTPAIR_PEER_CLOSED,
                             zx::deadline_after(zx::msec(1)),
                             &observed_signals)) {
        case ZX_ERR_TIMED_OUT:
            // timeout implies peer-closed was not observed
            return true;
        case ZX_OK:
            return (observed_signals & ZX_EVENTPAIR_PEER_CLOSED) == 0;
        default:
            return false;
    }
}

bool IsPeerValid(zx_handle_t handle) {
    return IsPeerValid(zx::unowned_eventpair(handle));
}

template <typename T, size_t N>
constexpr uint32_t ArrayCount(T const (&array)[N]) {
    static_assert(N < UINT32_MAX, "Array is too large!");
    return N;
}

}

// C server implementation
namespace internal_c {

zx_status_t ConsumeSimpleStruct(void* ctx,
                                const fidl_test_llcpp_basictypes_SimpleStruct* arg,
                                fidl_txn_t* txn) {
    // Verify that all the handles are valid channels
    if (!IsPeerValid(arg->ep)) {
        return fidl_test_llcpp_basictypes_TestInterfaceConsumeSimpleStruct_reply(
            txn, ZX_ERR_INVALID_ARGS, -1);
    }
    for (auto& row : arg->arr) {
        for (auto& handle : row) {
            if (!IsPeerValid(handle)) {
                return fidl_test_llcpp_basictypes_TestInterfaceConsumeSimpleStruct_reply(
                    txn, ZX_ERR_INVALID_ARGS, -1);
            }
        }
    }
    // Close all the handles as part of consumption
    zx_handle_close(arg->ep);
    for (auto& row : arg->arr) {
        for (auto& handle : row) {
            zx_handle_close(handle);
        }
    }
    // Loop back field argument
    return fidl_test_llcpp_basictypes_TestInterfaceConsumeSimpleStruct_reply(
        txn, ZX_OK, arg->field);
}

zx_status_t ConsumeSimpleUnion(void* ctx,
                               const fidl_test_llcpp_basictypes_SimpleUnion* arg,
                               fidl_txn_t* txn) {
    if (arg->tag == fidl_test_llcpp_basictypes_SimpleUnionTag_field_a) {
        return fidl_test_llcpp_basictypes_TestInterfaceConsumeSimpleUnion_reply(
            txn, 0, arg->field_a);
    } else if (arg->tag == fidl_test_llcpp_basictypes_SimpleUnionTag_field_b) {
        return fidl_test_llcpp_basictypes_TestInterfaceConsumeSimpleUnion_reply(
            txn, 1, arg->field_b);
    } else {
        return fidl_test_llcpp_basictypes_TestInterfaceConsumeSimpleUnion_reply(
            txn, UINT32_MAX, -1);
    }
}

const fidl_test_llcpp_basictypes_TestInterface_ops_t kOps = {
    .ConsumeSimpleStruct = ConsumeSimpleStruct,
    .ConsumeSimpleUnion = ConsumeSimpleUnion,
};

zx_status_t ServerDispatch(void* ctx,
                           fidl_txn_t* txn,
                           fidl_msg_t* msg,
                           const fidl_test_llcpp_basictypes_TestInterface_ops_t* ops) {
    zx_status_t status = fidl_test_llcpp_basictypes_TestInterface_try_dispatch(ctx, txn, msg, ops);
    if (status == ZX_ERR_NOT_SUPPORTED) {
        zx_handle_close_many(msg->handles, msg->num_handles);
    }
    return status;
}

}

// LLCPP client tests: interop between C server and LLCPP client
namespace {

bool SpinUpAsyncCServerHelper(zx::channel server, async_loop_t** out_loop) {
    BEGIN_HELPER;

    async_loop_t* loop = nullptr;
    ASSERT_EQ(ZX_OK, async_loop_create(&kAsyncLoopConfigNoAttachToThread, &loop), "");
    ASSERT_EQ(ZX_OK, async_loop_start_thread(loop, "basictypes-dispatcher", NULL), "");

    async_dispatcher_t* dispatcher = async_loop_get_dispatcher(loop);
    fidl_bind(dispatcher,
              server.release(),
              (fidl_dispatch_t*) internal_c::ServerDispatch,
              NULL,
              &internal_c::kOps);
    *out_loop = loop;

    END_HELPER;
}

void TearDownAsyncCServerHelper(async_loop_t* loop) {
    async_loop_destroy(loop);
}

bool RawChannelCallStructTest() {
    BEGIN_TEST;

    zx::channel client, server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);

    async_loop_t* loop = nullptr;
    ASSERT_TRUE(SpinUpAsyncCServerHelper(std::move(server), &loop));

    // manually call the server using generated message definitions
    FIDL_ALIGNDECL uint8_t storage[512] = {};
    fidl::BytePart bytes(&storage[0], sizeof(storage));
    // trivial linearization except to set message length
    bytes.set_actual(
        sizeof(fidl::test::llcpp::basictypes::TestInterface::ConsumeSimpleStructRequest));
    fidl::DecodedMessage<fidl::test::llcpp::basictypes::TestInterface::ConsumeSimpleStructRequest>
        request(std::move(bytes));
    request.message()->_hdr.ordinal =
        fidl_test_llcpp_basictypes_TestInterfaceConsumeSimpleStructOrdinal;
    request.message()->arg.field = 123;
    // make sure array shape is as expected (5 by 4)
    constexpr size_t kNumRow = 5;
    constexpr size_t kNumCol = 4;
    constexpr size_t kNumHandlesInArray = kNumRow * kNumCol;
    static_assert(decltype(request.message()->arg.arr)::size() == kNumRow);
    static_assert(
        std::remove_reference_t<decltype(request.message()->arg.arr[0])>::size() == kNumCol);
    // insert handles to be sent over
    zx::eventpair single_handle_payload;
    zx::eventpair single_handle_ourside;
    ASSERT_EQ(zx::eventpair::create(0, &single_handle_ourside, &single_handle_payload), ZX_OK);
    std::unique_ptr<zx::eventpair[]> handle_payload(new zx::eventpair[kNumHandlesInArray]);
    std::unique_ptr<zx::eventpair[]> handle_our_side(new zx::eventpair[kNumHandlesInArray]);
    for (size_t i = 0; i < kNumHandlesInArray; i++) {
        ASSERT_EQ(zx::eventpair::create(0, &handle_our_side[i], &handle_payload[i]), ZX_OK);
    }
    // fill the |ep| field
    request.message()->arg.ep = std::move(single_handle_payload);
    // fill the 2D handles array
    for (size_t i = 0; i < kNumRow; i++) {
        for (size_t j = 0; j < kNumCol; j++) {
            request.message()->arg.arr[i][j] = std::move(handle_payload[i * kNumCol + j]);
        }
    }
    auto encode_result = fidl::Encode(std::move(request));
    ASSERT_EQ(encode_result.status, ZX_OK);

    FIDL_ALIGNDECL uint8_t response_storage[512];
    fidl::BytePart response_bytes(&response_storage[0], sizeof(response_storage));
    auto response = fidl::Call(client, std::move(encode_result.message), std::move(response_bytes));

    ASSERT_EQ(response.status, ZX_OK);
    auto decode_result = fidl::Decode(std::move(response.message));
    ASSERT_EQ(decode_result.message.message()->field, 123);

    TearDownAsyncCServerHelper(loop);

    END_TEST;
}

bool RawChannelCallUnionTest() {
    BEGIN_TEST;

    zx::channel client, server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);

    async_loop_t* loop = nullptr;
    ASSERT_TRUE(SpinUpAsyncCServerHelper(std::move(server), &loop));

    // manually call the server using generated message definitions
    FIDL_ALIGNDECL uint8_t storage[512] = {};
    fidl::BytePart bytes(&storage[0], sizeof(storage));
    // trivial linearization except to set message length
    bytes.set_actual(
        sizeof(fidl::test::llcpp::basictypes::TestInterface::ConsumeSimpleUnionRequest));
    fidl::DecodedMessage<fidl::test::llcpp::basictypes::TestInterface::ConsumeSimpleUnionRequest>
        request(std::move(bytes));
    request.message()->_hdr.ordinal =
        fidl_test_llcpp_basictypes_TestInterfaceConsumeSimpleUnionOrdinal;
    request.message()->arg.mutable_field_b() = 456;

    auto encode_result = fidl::Encode(std::move(request));
    ASSERT_EQ(encode_result.status, ZX_OK);

    FIDL_ALIGNDECL uint8_t response_storage[512];
    fidl::BytePart response_bytes(&response_storage[0], sizeof(response_storage));
    auto response = fidl::Call(client, std::move(encode_result.message), std::move(response_bytes));

    ASSERT_EQ(response.status, ZX_OK);
    auto decode_result = fidl::Decode(std::move(response.message));
    ASSERT_EQ(decode_result.message.message()->index, 1);
    ASSERT_EQ(decode_result.message.message()->field, 456);

    TearDownAsyncCServerHelper(loop);

    END_TEST;
}

bool SyncCallStructTest() {
    BEGIN_TEST;

    zx::channel client, server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);

    async_loop_t* loop = nullptr;
    ASSERT_TRUE(SpinUpAsyncCServerHelper(std::move(server), &loop));

    // generated interface API
    fidl::test::llcpp::basictypes::TestInterface::SyncClient test(std::move(client));

    int32_t out_status;
    int32_t out_field;
    fidl::test::llcpp::basictypes::SimpleStruct simple_struct = {};
    simple_struct.field = 123;
    // make sure array shape is as expected (5 by 4)
    constexpr size_t kNumRow = 5;
    constexpr size_t kNumCol = 4;
    constexpr size_t kNumHandlesInArray = kNumRow * kNumCol;
    static_assert(decltype(simple_struct.arr)::size() == kNumRow);
    static_assert(
        std::remove_reference_t<decltype(simple_struct.arr[0])>::size() == kNumCol);
    // insert handles to be sent over
    zx::eventpair single_handle_payload;
    zx::eventpair single_handle_ourside;
    ASSERT_EQ(zx::eventpair::create(0, &single_handle_ourside, &single_handle_payload), ZX_OK);
    std::unique_ptr<zx::eventpair[]> handle_payload(new zx::eventpair[kNumHandlesInArray]);
    std::unique_ptr<zx::eventpair[]> handle_our_side(new zx::eventpair[kNumHandlesInArray]);
    for (size_t i = 0; i < kNumHandlesInArray; i++) {
        ASSERT_EQ(zx::eventpair::create(0, &handle_our_side[i], &handle_payload[i]), ZX_OK);
    }
    // fill the |ep| field
    simple_struct.ep = std::move(single_handle_payload);
    // fill the 2D handles array
    for (size_t i = 0; i < kNumRow; i++) {
        for (size_t j = 0; j < kNumCol; j++) {
            simple_struct.arr[i][j] = std::move(handle_payload[i * kNumCol + j]);
        }
    }
    // perform call
    zx_status_t status = test.ConsumeSimpleStruct(std::move(simple_struct),
                                                  &out_status,
                                                  &out_field);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(out_status, ZX_OK);
    ASSERT_EQ(out_field, 123);

    TearDownAsyncCServerHelper(loop);

    END_TEST;
}

bool SyncCallerAllocateCallStructTest() {
    BEGIN_TEST;

    zx::channel client, server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);

    async_loop_t* loop = nullptr;
    ASSERT_TRUE(SpinUpAsyncCServerHelper(std::move(server), &loop));

    // generated interface API
    fidl::test::llcpp::basictypes::TestInterface::SyncClient test(std::move(client));

    int32_t out_status;
    int32_t out_field;
    fidl::test::llcpp::basictypes::SimpleStruct simple_struct = {};
    simple_struct.field = 123;
    // make sure array shape is as expected (5 by 4)
    constexpr size_t kNumRow = 5;
    constexpr size_t kNumCol = 4;
    constexpr size_t kNumHandlesInArray = kNumRow * kNumCol;
    static_assert(decltype(simple_struct.arr)::size() == kNumRow);
    static_assert(
        std::remove_reference_t<decltype(simple_struct.arr[0])>::size() == kNumCol);
    // insert handles to be sent over
    zx::eventpair single_handle_payload;
    zx::eventpair single_handle_ourside;
    ASSERT_EQ(zx::eventpair::create(0, &single_handle_ourside, &single_handle_payload), ZX_OK);
    std::unique_ptr<zx::eventpair[]> handle_payload(new zx::eventpair[kNumHandlesInArray]);
    std::unique_ptr<zx::eventpair[]> handle_our_side(new zx::eventpair[kNumHandlesInArray]);
    for (size_t i = 0; i < kNumHandlesInArray; i++) {
        ASSERT_EQ(zx::eventpair::create(0, &handle_our_side[i], &handle_payload[i]), ZX_OK);
    }
    // fill the |ep| field
    simple_struct.ep = std::move(single_handle_payload);
    // fill the 2D handles array
    for (size_t i = 0; i < kNumRow; i++) {
        for (size_t j = 0; j < kNumCol; j++) {
            simple_struct.arr[i][j] = std::move(handle_payload[i * kNumCol + j]);
        }
    }

    // perform call
    FIDL_ALIGNDECL uint8_t request_buf[512] = {};
    FIDL_ALIGNDECL uint8_t response_buf[512] = {};
    auto result = test.ConsumeSimpleStruct(fidl::BytePart(request_buf, sizeof(request_buf)),
                                           std::move(simple_struct),
                                           fidl::BytePart(response_buf,
                                                          sizeof(response_buf)),
                                           &out_status, &out_field);
    ASSERT_EQ(result.status, ZX_OK);
    ASSERT_NULL(result.error, result.error);
    ASSERT_EQ(out_status, ZX_OK);
    ASSERT_EQ(out_field, 123);

    TearDownAsyncCServerHelper(loop);

    END_TEST;
}

bool SyncCallUnionTest() {
    BEGIN_TEST;

    zx::channel client, server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);

    async_loop_t* loop = nullptr;
    ASSERT_TRUE(SpinUpAsyncCServerHelper(std::move(server), &loop));

    // generated interface API
    fidl::test::llcpp::basictypes::TestInterface::SyncClient test(std::move(client));

    uint32_t out_index;
    int32_t out_field;
    fidl::test::llcpp::basictypes::SimpleUnion simple_union;
    simple_union.mutable_field_b() = 456;

    // perform call
    zx_status_t status = test.ConsumeSimpleUnion(std::move(simple_union), &out_index, &out_field);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(out_index, 1);
    ASSERT_EQ(out_field, 456);

    TearDownAsyncCServerHelper(loop);

    END_TEST;
}

bool SyncCallerAllocateCallUnionTest() {
    BEGIN_TEST;

    zx::channel client, server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);

    async_loop_t* loop = nullptr;
    ASSERT_TRUE(SpinUpAsyncCServerHelper(std::move(server), &loop));

    // generated interface API
    fidl::test::llcpp::basictypes::TestInterface::SyncClient test(std::move(client));

    uint32_t out_index;
    int32_t out_field;
    fidl::test::llcpp::basictypes::SimpleUnion simple_union;
    simple_union.mutable_field_b() = 456;

    // perform call
    FIDL_ALIGNDECL uint8_t request_buf[512] = {};
    FIDL_ALIGNDECL uint8_t response_buf[512] = {};
    auto result = test.ConsumeSimpleUnion(fidl::BytePart(request_buf, sizeof(request_buf)),
                                          std::move(simple_union),
                                          fidl::BytePart(response_buf, sizeof(response_buf)),
                                          &out_index, &out_field);
    ASSERT_EQ(result.status, ZX_OK);
    ASSERT_NULL(result.error, result.error);
    ASSERT_EQ(out_index, 1);
    ASSERT_EQ(out_field, 456);

    TearDownAsyncCServerHelper(loop);

    END_TEST;
}

}

// LLCPP sync server tests: interop between C client and LLCPP server
namespace {

namespace gen = fidl::test::llcpp::basictypes;

class Server : public gen::TestInterface::Interface {
public:
    void ConsumeSimpleStruct(gen::SimpleStruct arg,
                             ConsumeSimpleStructCompleter::Sync txn) override {
        num_struct_calls_.fetch_add(1);
        // Verify that all the handles are valid channels
        if (!IsPeerValid(zx::unowned_eventpair(arg.ep))) {
            txn.Reply(ZX_ERR_INVALID_ARGS, -1);
            return;
        }
        for (auto& row : arg.arr) {
            for (auto& handle : row) {
                if (!IsPeerValid(zx::unowned_eventpair(handle))) {
                    txn.Reply(ZX_ERR_INVALID_ARGS, -1);
                    return;
                }
            }
        }
        // Loop back field argument
        txn.Reply(ZX_OK, arg.field);
    }

    void ConsumeSimpleUnion(gen::SimpleUnion arg,
                            ConsumeSimpleUnionCompleter::Sync txn) override {
        num_union_calls_.fetch_add(1);
        if (arg.is_field_a()) {
            txn.Reply(0, arg.field_a());
        } else if (arg.is_field_b()) {
            txn.Reply(1, arg.field_b());
        } else {
            txn.Reply(std::numeric_limits<uint32_t>::max(), -1);
        }
    }

    uint64_t num_struct_calls() const { return num_struct_calls_.load(); }
    uint64_t num_union_calls() const { return num_union_calls_.load(); }

private:
    std::atomic<uint64_t> num_struct_calls_ = 0;
    std::atomic<uint64_t> num_union_calls_ = 0;
};

bool SpinUp(zx::channel server, Server* impl, std::unique_ptr<async::Loop> *out_loop) {
    BEGIN_HELPER;

    auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToThread);
    zx_status_t status = fidl::Bind(loop->dispatcher(),
                                    std::move(server),
                                    impl);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(loop->StartThread("test_llcpp_basictypes_server"), ZX_OK);
    *out_loop = std::move(loop);

    END_HELPER;
}

bool ServerUnionTest() {
    BEGIN_TEST;

    Server server_impl;
    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    std::unique_ptr<async::Loop> loop;
    ASSERT_TRUE(SpinUp(std::move(server_chan), &server_impl, &loop));

    constexpr uint32_t kNumIterations = 100;
    for (uint32_t i = 0; i < kNumIterations; i++) {
        ASSERT_EQ(server_impl.num_struct_calls(), 0);
        ASSERT_EQ(server_impl.num_union_calls(), i);

        fidl_test_llcpp_basictypes_SimpleUnion simple_union = {};
        simple_union.tag = fidl_test_llcpp_basictypes_SimpleUnionTag_field_a;
        simple_union.field_a = 5;
        uint32_t index = std::numeric_limits<uint32_t>::max();
        int32_t field;
        ASSERT_EQ(fidl_test_llcpp_basictypes_TestInterfaceConsumeSimpleUnion(client_chan.get(),
                                                                             &simple_union,
                                                                             &index,
                                                                             &field),
                  ZX_OK);
        ASSERT_EQ(index, 0);
        ASSERT_EQ(field, 5);
    }
    ASSERT_EQ(server_impl.num_union_calls(), kNumIterations);

    END_TEST;
}

bool ServerStructTest() {
    BEGIN_TEST;

    Server server_impl;
    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    std::unique_ptr<async::Loop> loop;
    ASSERT_TRUE(SpinUp(std::move(server_chan), &server_impl, &loop));

    fidl_test_llcpp_basictypes_SimpleStruct simple_struct = {};
    simple_struct.field = 123;
    // make sure array shape is as expected (5 by 4)
    constexpr size_t kNumRow = 5;
    constexpr size_t kNumCol = 4;
    constexpr size_t kNumHandlesInArray = kNumRow * kNumCol;
    static_assert(ArrayCount(simple_struct.arr) == kNumRow);
    static_assert(ArrayCount(simple_struct.arr[0]) == kNumCol);
    // insert handles to be sent over
    zx::eventpair single_handle_payload;
    zx::eventpair single_handle_ourside;
    ASSERT_EQ(zx::eventpair::create(0, &single_handle_ourside, &single_handle_payload), ZX_OK);
    std::unique_ptr<zx::eventpair[]> handle_payload(new zx::eventpair[kNumHandlesInArray]);
    std::unique_ptr<zx::eventpair[]> handle_our_side(new zx::eventpair[kNumHandlesInArray]);
    for (size_t i = 0; i < kNumHandlesInArray; i++) {
        ASSERT_EQ(zx::eventpair::create(0, &handle_our_side[i], &handle_payload[i]), ZX_OK);
    }
    // fill the |ep| field
    simple_struct.ep = single_handle_payload.release();
    // fill the 2D handles array
    for (size_t i = 0; i < kNumRow; i++) {
        for (size_t j = 0; j < kNumCol; j++) {
            simple_struct.arr[i][j] = handle_payload[i * kNumCol + j].release();
        }
    }

    // call
    int32_t out_status;
    int32_t out_field;
    zx_status_t status = fidl_test_llcpp_basictypes_TestInterfaceConsumeSimpleStruct(
        client_chan.get(), &simple_struct, &out_status, &out_field);

    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(out_status, ZX_OK);
    ASSERT_EQ(out_field, 123);
    ASSERT_EQ(server_impl.num_struct_calls(), 1);
    ASSERT_EQ(server_impl.num_union_calls(), 0);

    END_TEST;
}

}

BEGIN_TEST_CASE(llcpp_basictypes_tests)
RUN_NAMED_TEST_SMALL("client: raw channel call (passing struct)", RawChannelCallStructTest)
RUN_NAMED_TEST_SMALL("client: raw channel call (passing union)", RawChannelCallUnionTest)
RUN_NAMED_TEST_SMALL("client: generated binding (passing struct)", SyncCallStructTest)
RUN_NAMED_TEST_SMALL("client: generated binding (passing union)", SyncCallUnionTest)
RUN_NAMED_TEST_SMALL("client: generated binding (passing struct, caller allocating)",
                     SyncCallerAllocateCallStructTest)
RUN_NAMED_TEST_SMALL("client: generated binding (passing union, caller allocating)",
                     SyncCallerAllocateCallUnionTest)
RUN_NAMED_TEST_SMALL("server: passing union", ServerUnionTest)
RUN_NAMED_TEST_SMALL("server: passing struct", ServerStructTest)
END_TEST_CASE(llcpp_basictypes_tests)
