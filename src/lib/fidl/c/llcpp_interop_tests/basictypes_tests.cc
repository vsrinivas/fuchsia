// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/time.h>
#include <string.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <atomic>
#include <memory>
#include <utility>

#include <fidl/test/llcpp/basictypes/c/fidl.h>
#include <zxtest/zxtest.h>

// Interface under test
#include <fidl/test/llcpp/basictypes/llcpp/fidl.h>

namespace basictypes = llcpp::fidl::test::llcpp::basictypes;

// test utility functions
namespace {

bool IsPeerValid(const zx::unowned_eventpair& handle) {
  zx_signals_t observed_signals = {};
  switch (handle->wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::deadline_after(zx::msec(1)),
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

bool IsPeerValid(zx_handle_t handle) { return IsPeerValid(zx::unowned_eventpair(handle)); }

template <typename T, size_t N>
constexpr uint32_t ArrayCount(T const (&array)[N]) {
  static_assert(N < UINT32_MAX, "Array is too large!");
  return N;
}

}  // namespace

// C server implementation
namespace internal_c {

zx_status_t ConsumeSimpleStruct(void* ctx, const fidl_test_llcpp_basictypes_SimpleStruct* arg,
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
  return fidl_test_llcpp_basictypes_TestInterfaceConsumeSimpleStruct_reply(txn, ZX_OK, arg->field);
}

const fidl_test_llcpp_basictypes_TestInterface_ops_t kOps = {
    .ConsumeSimpleStruct = ConsumeSimpleStruct,
};

zx_status_t ServerDispatch(void* ctx, fidl_txn_t* txn, fidl_incoming_msg_t* msg,
                           const fidl_test_llcpp_basictypes_TestInterface_ops_t* ops) {
  zx_status_t status = fidl_test_llcpp_basictypes_TestInterface_try_dispatch(ctx, txn, msg, ops);
  if (status == ZX_ERR_NOT_SUPPORTED) {
    zx_handle_close_many(msg->handles, msg->num_handles);
  }
  return status;
}

}  // namespace internal_c

// LLCPP client tests: interop between C server and LLCPP client
namespace {

void SpinUpAsyncCServerHelper(zx::channel server, async_loop_t** out_loop) {
  async_loop_t* loop = nullptr;
  ASSERT_OK(async_loop_create(&kAsyncLoopConfigNoAttachToCurrentThread, &loop), "");
  ASSERT_OK(async_loop_start_thread(loop, "basictypes-dispatcher", NULL), "");

  async_dispatcher_t* dispatcher = async_loop_get_dispatcher(loop);
  fidl_bind(dispatcher, server.release(), (fidl_dispatch_t*)internal_c::ServerDispatch, NULL,
            &internal_c::kOps);
  *out_loop = loop;
}

void TearDownAsyncCServerHelper(async_loop_t* loop) { async_loop_destroy(loop); }

}  // namespace

template <typename T>
void WithEncodedMessage(T callback) {
  // manually call the server using generated message definitions
  FIDL_ALIGNDECL uint8_t storage[512] = {};
  fidl::BytePart bytes(storage, sizeof(storage),
                       sizeof(basictypes::TestInterface::ConsumeSimpleStructRequest));
  new (storage) basictypes::TestInterface::ConsumeSimpleStructRequest(0);
  fidl::DecodedMessage<basictypes::TestInterface::ConsumeSimpleStructRequest> request(
      std::move(bytes));
  request.message()->arg.field = 123;
  // make sure array shape is as expected (5 by 4)
  constexpr size_t kNumRow = 5;
  constexpr size_t kNumCol = 4;
  constexpr size_t kNumHandlesInArray = kNumRow * kNumCol;
  static_assert(decltype(request.message()->arg.arr)::size() == kNumRow);
  static_assert(std::remove_reference_t<decltype(request.message()->arg.arr[0])>::size() ==
                kNumCol);
  // insert handles to be sent over
  zx::eventpair single_handle_payload;
  zx::eventpair single_handle_ourside;
  ASSERT_OK(zx::eventpair::create(0, &single_handle_ourside, &single_handle_payload));
  std::unique_ptr<zx::eventpair[]> handle_payload(new zx::eventpair[kNumHandlesInArray]);
  std::unique_ptr<zx::eventpair[]> handle_our_side(new zx::eventpair[kNumHandlesInArray]);
  for (size_t i = 0; i < kNumHandlesInArray; i++) {
    ASSERT_OK(zx::eventpair::create(0, &handle_our_side[i], &handle_payload[i]));
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
  ASSERT_OK(encode_result.status);

  callback(std::move(encode_result));
}

TEST(BasicTypesTest, RawChannelCallStruct) {
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));

  async_loop_t* loop = nullptr;
  ASSERT_NO_FATAL_FAILURES(SpinUpAsyncCServerHelper(std::move(server), &loop));

  WithEncodedMessage(
      [&](fidl::EncodeResult<basictypes::TestInterface::ConsumeSimpleStructRequest> encode_result) {
        FIDL_ALIGNDECL uint8_t response_storage[512];
        fidl::BytePart response_bytes(&response_storage[0], sizeof(response_storage));
        auto response =
            fidl::Call(client, std::move(encode_result.message), std::move(response_bytes));

        ASSERT_OK(response.status);
        auto decode_result = fidl::Decode(std::move(response.message));
        ASSERT_EQ(decode_result.message.message()->field, 123);
      });

  TearDownAsyncCServerHelper(loop);
}

TEST(BasicTypesTest, RawChannelCallStructWithTimeout) {
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));

  async_loop_t* loop = nullptr;
  ASSERT_NO_FATAL_FAILURES(SpinUpAsyncCServerHelper(std::move(server), &loop));

  WithEncodedMessage(
      [&](fidl::EncodeResult<basictypes::TestInterface::ConsumeSimpleStructRequest> encode_result) {
        FIDL_ALIGNDECL uint8_t response_storage[512];
        fidl::BytePart response_bytes(&response_storage[0], sizeof(response_storage));
        auto response = fidl::Call(client, std::move(encode_result.message),
                                   std::move(response_bytes), zx::time::infinite_past());

        ASSERT_EQ(ZX_ERR_TIMED_OUT, response.status);
      });

  TearDownAsyncCServerHelper(loop);
}

TEST(BasicTypesTest, SyncCallStruct) {
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));

  async_loop_t* loop = nullptr;
  ASSERT_NO_FATAL_FAILURES(SpinUpAsyncCServerHelper(std::move(server), &loop));

  // generated interface API
  basictypes::TestInterface::SyncClient test(std::move(client));

  basictypes::SimpleStruct simple_struct = {};
  simple_struct.field = 123;
  // make sure array shape is as expected (5 by 4)
  constexpr size_t kNumRow = 5;
  constexpr size_t kNumCol = 4;
  constexpr size_t kNumHandlesInArray = kNumRow * kNumCol;
  static_assert(decltype(simple_struct.arr)::size() == kNumRow);
  static_assert(std::remove_reference_t<decltype(simple_struct.arr[0])>::size() == kNumCol);
  // insert handles to be sent over
  zx::eventpair single_handle_payload;
  zx::eventpair single_handle_ourside;
  ASSERT_OK(zx::eventpair::create(0, &single_handle_ourside, &single_handle_payload));
  std::unique_ptr<zx::eventpair[]> handle_payload(new zx::eventpair[kNumHandlesInArray]);
  std::unique_ptr<zx::eventpair[]> handle_our_side(new zx::eventpair[kNumHandlesInArray]);
  for (size_t i = 0; i < kNumHandlesInArray; i++) {
    ASSERT_OK(zx::eventpair::create(0, &handle_our_side[i], &handle_payload[i]));
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
  auto result = test.ConsumeSimpleStruct(std::move(simple_struct));
  ASSERT_OK(result.status());
  ASSERT_OK(result.Unwrap()->status);
  ASSERT_EQ(result.Unwrap()->field, 123);

  TearDownAsyncCServerHelper(loop);
}

TEST(BasicTypesTest, SyncCallerAllocateCallStruct) {
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));

  async_loop_t* loop = nullptr;
  ASSERT_NO_FATAL_FAILURES(SpinUpAsyncCServerHelper(std::move(server), &loop));

  // generated interface API
  basictypes::TestInterface::SyncClient test(std::move(client));

  basictypes::SimpleStruct simple_struct = {};
  simple_struct.field = 123;
  // make sure array shape is as expected (5 by 4)
  constexpr size_t kNumRow = 5;
  constexpr size_t kNumCol = 4;
  constexpr size_t kNumHandlesInArray = kNumRow * kNumCol;
  static_assert(decltype(simple_struct.arr)::size() == kNumRow);
  static_assert(std::remove_reference_t<decltype(simple_struct.arr[0])>::size() == kNumCol);
  // insert handles to be sent over
  zx::eventpair single_handle_payload;
  zx::eventpair single_handle_ourside;
  ASSERT_OK(zx::eventpair::create(0, &single_handle_ourside, &single_handle_payload));
  std::unique_ptr<zx::eventpair[]> handle_payload(new zx::eventpair[kNumHandlesInArray]);
  std::unique_ptr<zx::eventpair[]> handle_our_side(new zx::eventpair[kNumHandlesInArray]);
  for (size_t i = 0; i < kNumHandlesInArray; i++) {
    ASSERT_OK(zx::eventpair::create(0, &handle_our_side[i], &handle_payload[i]));
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
                                         fidl::BytePart(response_buf, sizeof(response_buf)));
  ASSERT_OK(result.status());
  ASSERT_NULL(result.error(), "%s", result.error());
  ASSERT_OK(result.Unwrap()->status);
  ASSERT_EQ(result.Unwrap()->field, 123);

  TearDownAsyncCServerHelper(loop);
}

// LLCPP sync server tests: interop between C client and LLCPP server
namespace {

namespace gen = llcpp::fidl::test::llcpp::basictypes;

class Server : public gen::TestInterface::Interface {
 public:
  void ConsumeSimpleStruct(gen::SimpleStruct arg,
                           ConsumeSimpleStructCompleter::Sync& txn) override {
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

  uint64_t num_struct_calls() const { return num_struct_calls_.load(); }

 private:
  std::atomic<uint64_t> num_struct_calls_ = 0;
};

}  // namespace

void SpinUp(zx::channel server, Server* impl, std::unique_ptr<async::Loop>* out_loop) {
  auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
  zx_status_t status = fidl::BindSingleInFlightOnly(loop->dispatcher(), std::move(server), impl);
  ASSERT_OK(status);
  ASSERT_OK(loop->StartThread("test_llcpp_basictypes_server"));
  *out_loop = std::move(loop);
}

TEST(BasicTypesTest, ServerStruct) {
  Server server_impl;
  zx::channel client_chan, server_chan;
  ASSERT_OK(zx::channel::create(0, &client_chan, &server_chan));
  std::unique_ptr<async::Loop> loop;
  ASSERT_NO_FATAL_FAILURES(SpinUp(std::move(server_chan), &server_impl, &loop));

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
  ASSERT_OK(zx::eventpair::create(0, &single_handle_ourside, &single_handle_payload));
  std::unique_ptr<zx::eventpair[]> handle_payload(new zx::eventpair[kNumHandlesInArray]);
  std::unique_ptr<zx::eventpair[]> handle_our_side(new zx::eventpair[kNumHandlesInArray]);
  for (size_t i = 0; i < kNumHandlesInArray; i++) {
    ASSERT_OK(zx::eventpair::create(0, &handle_our_side[i], &handle_payload[i]));
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

  ASSERT_OK(status);
  ASSERT_OK(out_status);
  ASSERT_EQ(out_field, 123);
  ASSERT_EQ(server_impl.num_struct_calls(), 1);
}
