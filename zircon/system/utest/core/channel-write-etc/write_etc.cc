// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/fifo.h>
#include <lib/zx/object.h>
#include <lib/zx/socket.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <vector>

#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

namespace {

// Data used for writing into a channel.
constexpr uint32_t kChannelData = 0xbaadcafe;

void GetBasicInfo(zx_handle_t handle, zx_info_handle_basic_t* info) {
  ASSERT_OK(zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, info, sizeof(zx_info_handle_basic_t),
                               nullptr, nullptr));
}
zx_status_t ExpectedCloseResult(zx_handle_op_t op) {
  if (op == ZX_HANDLE_OP_DUPLICATE)
    return ZX_OK;
  else
    return ZX_ERR_BAD_HANDLE;
}

const std::string test_case_str[] = {"Test case operation arg: ZX_HANDLE_OP_MOVE",
                                     "Test case operation arg: ZX_HANDLE_OP_DUPLICATE"};

TEST(ChannelWriteEtcTest, MultipleHandlesSomeInvalidResultsReportedCorrectly) {
  zx::channel channel_local, channel_remote, channel_arg_local, channel_arg_remote;
  zx::socket socket_local, socket_remote;
  zx::event event;

  ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));
  ASSERT_OK(zx::channel::create(0, &channel_arg_local, &channel_arg_remote));
  ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));
  ASSERT_OK(zx::event::create(0, &event));

  zx_handle_t socket_local_handle = socket_local.release();
  zx_handle_t socket_remote_handle = socket_remote.release();
  zx_handle_t event_handle = event.release();
  zx_handle_t channel_handle = channel_arg_local.release();

  // socket_local_handle wrong type. sock_remote_handle OK. channel_handle can't be duped,
  // event_handle OK.
  zx_handle_disposition_t send_handle_list[] = {
      {ZX_HANDLE_OP_MOVE, socket_local_handle, ZX_OBJ_TYPE_PORT, ZX_RIGHT_SAME_RIGHTS, ZX_OK},
      {ZX_HANDLE_OP_MOVE, socket_remote_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK},
      {ZX_HANDLE_OP_DUPLICATE, channel_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK},
      {ZX_HANDLE_OP_MOVE, event_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

  EXPECT_EQ(ZX_ERR_WRONG_TYPE,
            channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                    countof(send_handle_list)));

  EXPECT_EQ(ZX_ERR_WRONG_TYPE, send_handle_list[0].result);
  EXPECT_EQ(ZX_OK, send_handle_list[1].result);
  EXPECT_EQ(ZX_ERR_ACCESS_DENIED, send_handle_list[2].result);
  EXPECT_EQ(ZX_OK, send_handle_list[3].result);

  EXPECT_OK(zx_handle_close(channel_handle));
}

TEST(ChannelWriteEtcTest, ImproperlyInitalizedResultsArgReportedBackAsOriginallyInitalized) {
  zx::channel channel_local, channel_remote;
  zx::event event;

  ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));
  ASSERT_OK(zx::event::create(0, &event));

  zx_handle_t event_handle = event.release();

  zx_handle_disposition_t send_handle_list[] = {
      {ZX_HANDLE_OP_MOVE, event_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_ERR_WRONG_TYPE}};

  EXPECT_OK(channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                    countof(send_handle_list)));

  EXPECT_EQ(ZX_ERR_WRONG_TYPE, send_handle_list[0].result);
}

TEST(ChannelWriteEtcTest, FailureDoesNotResultInReceivedPacket) {
  zx::channel channel_local, channel_remote;
  zx::event event;

  ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));
  ASSERT_OK(zx::event::create(0, &event));

  zx_handle_t event_handle = event.release();

  zx_handle_disposition_t send_handle_list[] = {
      {ZX_HANDLE_OP_MOVE, event_handle, ZX_OBJ_TYPE_SOCKET, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

  EXPECT_EQ(ZX_ERR_WRONG_TYPE,
            channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                    countof(send_handle_list)));
  uint32_t incoming_bytes;
  zx_handle_t incoming_handle;
  ASSERT_EQ(ZX_ERR_SHOULD_WAIT, channel_remote.read(0, &incoming_bytes, &incoming_handle,
                                                    sizeof(incoming_bytes), 1, nullptr, nullptr));
}

TEST(ChannelWriteEtcTest, SentHandleReferrsToSameObject) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result) {
    zx::channel channel_local, channel_remote;
    zx::event event;

    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));
    ASSERT_OK(zx::event::create(0, &event));

    zx_handle_t event_handle = event.release();

    zx_handle_disposition_t send_handle_list[] = {
        {op, event_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

    zx_info_handle_basic_t event_info = {};
    GetBasicInfo(event_handle, &event_info);

    ASSERT_OK(channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                      countof(send_handle_list)),
              "%s", test_case_str[op].c_str());
    ASSERT_EQ(expected_close_result, zx_handle_close(event_handle), "%s",
              test_case_str[op].c_str());

    uint32_t incoming_bytes;
    zx_handle_t incoming_handle;
    ASSERT_OK(channel_remote.read(0, &incoming_bytes, &incoming_handle, sizeof(incoming_bytes), 1,
                                  nullptr, nullptr),
              "%s", test_case_str[op].c_str());

    zx_info_handle_basic_t incoming_event_info = {};
    GetBasicInfo(incoming_handle, &incoming_event_info);

    EXPECT_EQ(event_info.koid, incoming_event_info.koid, "%s", test_case_str[op].c_str());
    EXPECT_EQ(event_info.rights, incoming_event_info.rights, "%s", test_case_str[op].c_str());
    EXPECT_EQ(event_info.type, incoming_event_info.type, "%s", test_case_str[op].c_str());
    EXPECT_OK(zx_handle_close(incoming_handle), "%s", test_case_str[op].c_str());
  };

  TestHelper(ZX_HANDLE_OP_MOVE, ZX_ERR_BAD_HANDLE);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_OK);
}

TEST(ChannelWriteEtcTest, InvalidOpArgShouldFail) {
  auto TestHelper = [](zx_handle_op_t op, std::string op_str, zx_status_t expected_close_result) {
    zx::socket socket_local, socket_remote;
    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));
    zx_handle_t socket_local_handle = socket_local.release();

    zx_handle_disposition_t send_handle_list[] = {
        {op, socket_local_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

    zx::channel channel_local, channel_remote;
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

    EXPECT_EQ(ZX_ERR_INVALID_ARGS,
              channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                      countof(send_handle_list)),
              "%s", op_str.c_str());

    EXPECT_EQ(expected_close_result, zx_handle_close(socket_local_handle), "%s", op_str.c_str());
  };

  TestHelper(0xffffffff, "op arg: 0xffffffff", ZX_ERR_BAD_HANDLE);
  TestHelper(2, "op arg: 2", ZX_ERR_BAD_HANDLE);
}

TEST(ChannelWriteEtcTest, HandleArgNotAChannelHandleShouldFail) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result) {
    zx::socket socket_local, socket_remote;
    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));

    zx_handle_t socket_local_handle = socket_local.release();
    zx_handle_t socket_remote_handle = socket_remote.release();

    zx_handle_disposition_t send_handle_list[] = {
        {op, socket_remote_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

    ASSERT_EQ(ZX_ERR_WRONG_TYPE,
              zx_channel_write_etc(socket_local_handle, 0, &kChannelData, sizeof(kChannelData),
                                   send_handle_list, countof(send_handle_list)));

    ASSERT_OK(zx_handle_close(socket_local_handle));
    ASSERT_EQ(expected_close_result, zx_handle_close(socket_remote_handle), "%s",
              test_case_str[op].c_str());
  };

  TestHelper(ZX_HANDLE_OP_MOVE, ZX_ERR_BAD_HANDLE);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_OK);
}

TEST(ChannelWriteEtcTest, ChannelHandleNotValidShouldFail) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result) {
    zx::socket socket_local, socket_remote;
    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));
    zx_handle_t socket_local_handle = socket_local.release();

    zx_handle_disposition_t send_handle_list[] = {
        {op, socket_local_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

    zx::channel channel_local, channel_remote;
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));
    zx_handle_t channel_local_handle = channel_local.release();
    ASSERT_OK(zx_handle_close(channel_local_handle));

    EXPECT_EQ(ZX_ERR_BAD_HANDLE,
              zx_channel_write_etc(channel_local_handle, 0, &kChannelData, sizeof(kChannelData),
                                   send_handle_list, countof(send_handle_list)));

    EXPECT_EQ(expected_close_result, zx_handle_close(socket_local_handle), "%s",
              test_case_str[op].c_str());
  };

  TestHelper(ZX_HANDLE_OP_MOVE, ZX_ERR_BAD_HANDLE);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_OK);
}

TEST(ChannelWriteEtcTest, ChannelHandleWithoutWriteRightShouldFail) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result) {
    zx::channel channel_local, channel_remote, channel_local_no_write;
    zx::socket socket_local, socket_remote;

    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

    zx_info_handle_basic_t channel_local_info = {};
    GetBasicInfo(channel_local.get(), &channel_local_info);

    zx_handle_t socket_local_handle = socket_local.release();
    zx_handle_disposition_t send_handle_list = {op, socket_local_handle, ZX_OBJ_TYPE_NONE,
                                                ZX_RIGHT_SAME_RIGHTS, ZX_OK};

    // Removing mandatory ZX_RIGHT_WRITE should fail
    ASSERT_OK(channel_local.replace(channel_local_info.rights & ~ZX_RIGHT_WRITE,
                                    &channel_local_no_write));
    ASSERT_EQ(ZX_ERR_ACCESS_DENIED,
              channel_local_no_write.write_etc(0, &kChannelData, sizeof(kChannelData),
                                               &send_handle_list, 1));
    EXPECT_EQ(expected_close_result, zx_handle_close(socket_local_handle), "%s",
              test_case_str[op].c_str());
  };

  TestHelper(ZX_HANDLE_OP_MOVE, ZX_ERR_BAD_HANDLE);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_OK);
}

TEST(ChannelWriteEtcTest, HandleWithoutTransferRightShouldFail) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result) {
    zx::channel channel_local, channel_remote;
    zx::socket socket_local, socket_remote, socket_local_no_transfer;

    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

    zx_info_handle_basic_t socket_local_info = {};
    GetBasicInfo(socket_local.get(), &socket_local_info);

    ASSERT_OK(socket_local.replace(socket_local_info.rights & ~ZX_RIGHT_TRANSFER,
                                   &socket_local_no_transfer));

    zx_handle_t socket_local_no_transfer_handle = socket_local_no_transfer.release();
    zx_handle_disposition_t send_handle_list[] = {
        {op, socket_local_no_transfer_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

    ASSERT_EQ(ZX_ERR_ACCESS_DENIED,
              channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                      countof(send_handle_list)));
    EXPECT_EQ(expected_close_result, zx_handle_close(socket_local_no_transfer_handle), "%s",
              test_case_str[op].c_str());
  };

  TestHelper(ZX_HANDLE_OP_MOVE, ZX_ERR_BAD_HANDLE);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_OK);
}

TEST(ChannelWriteEtcTest, InvalidHandleInTransferredHandlesShouldFail) {
  auto TestHelper = [](zx_handle_t bad_handle, std::string testcase) {
    zx::channel channel_local, channel_remote;
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));
    zx_handle_disposition_t send_handle_list[] = {
        {ZX_HANDLE_OP_DUPLICATE, bad_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

    EXPECT_EQ(ZX_ERR_BAD_HANDLE,
              channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                      countof(send_handle_list)),
              "test case: %s", testcase.c_str());
  };

  zx::socket socket_local, socket_remote;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));
  zx_handle_t socket_handle = socket_local.release();
  EXPECT_OK(zx_handle_close(socket_handle));

  TestHelper(socket_handle, "closed socket handle");
  TestHelper(ZX_HANDLE_INVALID, "ZX_HANDLE_INVALID");
  TestHelper(0xffffffff, "0xffffffff");
}

TEST(ChannelWriteEtcTest, RepeatedHandlesWithOpMoveHandlesShouldFail) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result) {
    zx::channel channel_local, channel_remote;
    zx::socket socket_local, socket_remote;

    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));
    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));

    zx_handle_t socket_local_handle = socket_local.release();

    zx_handle_disposition_t send_handle_list[] = {
        {op, socket_local_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK},
        {op, socket_local_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

    EXPECT_EQ(ZX_ERR_BAD_HANDLE,
              channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                      countof(send_handle_list)),
              "%s", test_case_str[op].c_str());

    EXPECT_EQ(expected_close_result, zx_handle_close(socket_local_handle), "%s",
              test_case_str[op].c_str());
  };
  TestHelper(ZX_HANDLE_OP_MOVE, ZX_ERR_BAD_HANDLE);
}

TEST(ChannelWriteEtcTest, DuplicateHandlesInTransferredHandlesShouldSucceed) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result) {
    zx::channel channel_local, channel_remote;
    zx::socket socket_local, socket_remote, socket_local_duplicate;

    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));
    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));
    ASSERT_OK(socket_local.duplicate(ZX_RIGHT_SAME_RIGHTS, &socket_local_duplicate));

    zx_handle_t socket_local_handle = socket_local.release();
    zx_handle_t socket_local_duplicate_handle = socket_local_duplicate.release();

    zx_handle_disposition_t send_handle_list[] = {
        {op, socket_local_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK},
        {op, socket_local_duplicate_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

    EXPECT_OK(channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                      countof(send_handle_list)),
              "%s", test_case_str[op].c_str());
    EXPECT_EQ(expected_close_result, zx_handle_close(socket_local_handle), "%s",
              test_case_str[op].c_str());
    EXPECT_EQ(expected_close_result, zx_handle_close(socket_local_duplicate_handle), "%s",
              test_case_str[op].c_str());
  };
  TestHelper(ZX_HANDLE_OP_MOVE, ZX_ERR_BAD_HANDLE);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_OK);
}

TEST(ChannelWriteEtcTest, HandleDoesNotMatchTypeShouldFail) {
  auto TestHelper = [](zx_handle_op_t op, zx_obj_type_t obj_type,
                       zx_status_t expected_close_result) {
    zx::channel channel_local, channel_remote;
    zx::socket socket_local, socket_remote;

    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

    zx_handle_t socket_local_handle = socket_local.release();

    zx_handle_disposition_t send_handle_list[] = {
        {op, socket_local_handle, obj_type, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

    EXPECT_EQ(ZX_ERR_WRONG_TYPE,
              channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                      countof(send_handle_list)),
              "test case: obj_type: %u op: %d", obj_type, op);

    EXPECT_EQ(expected_close_result, zx_handle_close(socket_local_handle),
              "test case: obj_type: %u op: %d", obj_type, op);
  };

  for (zx_obj_type_t obj_type = 0; obj_type < 256; obj_type++) {
    if ((obj_type == ZX_OBJ_TYPE_SOCKET) || (obj_type == ZX_OBJ_TYPE_NONE))
      continue;
    TestHelper(ZX_HANDLE_OP_MOVE, obj_type, ZX_ERR_BAD_HANDLE);
    TestHelper(ZX_HANDLE_OP_DUPLICATE, obj_type, ZX_OK);
  }
}

TEST(ChannelWriteEtcTest, OptionsArgNonZeroShouldFail) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result) {
    zx::channel channel_local, channel_remote;
    zx::socket socket_local, socket_remote;

    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

    zx_handle_t socket_local_handle = socket_local.release();

    zx_handle_disposition_t send_handle_list[] = {
        {op, socket_local_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

    ASSERT_EQ(ZX_ERR_INVALID_ARGS,
              channel_local.write_etc(1, &kChannelData, sizeof(kChannelData), send_handle_list,
                                      countof(send_handle_list)));

    ASSERT_EQ(expected_close_result, zx_handle_close(socket_local_handle), "%s",
              test_case_str[op].c_str());
  };
  TestHelper(ZX_HANDLE_OP_MOVE, ZX_ERR_BAD_HANDLE);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_OK);
}

TEST(ChannelWriteEtcTest, ChannelHandleInTransferredHandlesShouldFail) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result) {
    zx_handle_t channel_local, channel_remote;

    ASSERT_OK(zx_channel_create(0, &channel_local, &channel_remote));

    zx_handle_disposition_t send_handle_list[] = {
        {op, channel_local, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED,
              zx_channel_write_etc(channel_local, 0, &kChannelData, sizeof(kChannelData),
                                   send_handle_list, countof(send_handle_list)));

    ASSERT_EQ(expected_close_result, zx_handle_close(channel_local), "%s",
              test_case_str[op].c_str());
    ASSERT_OK(zx_handle_close(channel_remote), "%s", test_case_str[op].c_str());
  };
  TestHelper(ZX_HANDLE_OP_MOVE, ZX_ERR_BAD_HANDLE);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_OK);
}

TEST(ChannelWriteEtcTest, OppositeChannelEndClosedShouldFail) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result) {
    zx::channel channel_local, channel_remote;
    zx::socket socket_local, socket_remote;

    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

    zx_handle_t socket_local_handle = socket_local.release();
    channel_remote.reset();

    zx_handle_disposition_t send_handle_list[] = {
        {op, socket_local_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

    ASSERT_EQ(ZX_ERR_PEER_CLOSED,
              channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                      countof(send_handle_list)));

    ASSERT_EQ(expected_close_result, zx_handle_close(socket_local_handle), "%s",
              test_case_str[op].c_str());
  };

  TestHelper(ZX_HANDLE_OP_MOVE, ZX_ERR_BAD_HANDLE);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_OK);
}

TEST(ChannelWriteEtcTest, HandleCountBoundaryChecks) {
  auto TestHelper = [](zx_handle_op_t op, uint32_t num_handles, zx_status_t expected_write_result) {
    zx::channel channel_local, channel_remote;
    zx::event event[ZX_CHANNEL_MAX_MSG_HANDLES + 1];
    zx_handle_t event_handle[ZX_CHANNEL_MAX_MSG_HANDLES + 1];

    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

    zx_handle_disposition_t send_handle_list[ZX_CHANNEL_MAX_MSG_HANDLES + 1];

    for (uint32_t i = 0; i < num_handles; ++i) {
      ASSERT_OK(zx::event::create(0, &event[i]));
      event_handle[i] = event[i].release();
      send_handle_list[i] = {op, event_handle[i], ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK};
    }

    ASSERT_EQ(expected_write_result,
              channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                      num_handles),
              "test case num_handles: %d op: %d", num_handles, op);

    for (uint32_t i = 0; i < num_handles; ++i) {
      EXPECT_EQ(ExpectedCloseResult(op), zx_handle_close(event_handle[i]),
                "test case num_handles: %d op: %d i: %d", num_handles, op, i);
    }
  };

  TestHelper(ZX_HANDLE_OP_MOVE, 0, ZX_OK);
  TestHelper(ZX_HANDLE_OP_MOVE, ZX_CHANNEL_MAX_MSG_HANDLES, ZX_OK);
  TestHelper(ZX_HANDLE_OP_MOVE, ZX_CHANNEL_MAX_MSG_HANDLES + 1, ZX_ERR_OUT_OF_RANGE);

  TestHelper(ZX_HANDLE_OP_DUPLICATE, 0, ZX_OK);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_CHANNEL_MAX_MSG_HANDLES, ZX_OK);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_CHANNEL_MAX_MSG_HANDLES + 1, ZX_ERR_OUT_OF_RANGE);
}

TEST(ChannelWriteEtcTest, HandleCountAndDataCountBothZeroShouldSucceed) {
  zx::channel channel_local, channel_remote;

  ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

  ASSERT_EQ(ZX_OK, channel_local.write_etc(0, nullptr, 0, nullptr, 0));
}

TEST(ChannelWriteEtcTest, MaximumNumberHandlesWithZeroCountArrayArgShouldSucceed) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result) {
    zx::channel channel_local, channel_remote;
    zx::event event[ZX_CHANNEL_MAX_MSG_HANDLES];
    zx_handle_t event_handle[ZX_CHANNEL_MAX_MSG_HANDLES];

    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

    zx_handle_disposition_t send_handle_list[ZX_CHANNEL_MAX_MSG_HANDLES];

    for (uint32_t i = 0; i < countof(send_handle_list); ++i) {
      ASSERT_OK(zx::event::create(0, &event[i]));
      event_handle[i] = event[i].release();
      send_handle_list[i] = {op, event_handle[i], ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK};
    }

    ASSERT_OK(channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list, 0));

    for (uint32_t i = 0; i < countof(event_handle); ++i) {
      ASSERT_EQ(expected_close_result, zx_handle_close(event_handle[i]), "%d: %s", i,
                test_case_str[op].c_str());
    }
  };

  // Since handle count set to 0, none of the handles should have been touched so even
  // the move case the handles should still be untouched and valid, and close without error.
  TestHelper(ZX_HANDLE_OP_MOVE, ZX_OK);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_OK);
}

TEST(ChannelWriteEtcTest, ByteCountIsMaxShouldSucceed) {
  zx::channel channel_local, channel_remote;

  ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

  uint8_t byte_array[ZX_CHANNEL_MAX_MSG_BYTES];

  ASSERT_EQ(ZX_OK, channel_local.write_etc(0, &byte_array, sizeof(byte_array), nullptr, 0));
}

TEST(ChannelWriteEtcTest, ByteCountIsMaxPlusOneShouldFail) {
  zx::channel channel_local, channel_remote;

  ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

  uint8_t byte_array[ZX_CHANNEL_MAX_MSG_BYTES + 1];

  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE,
            channel_local.write_etc(0, &byte_array, sizeof(byte_array), nullptr, 0));
}

TEST(ChannelWriteEtcTest, NullptrArgWhenSizeNonZeroShouldFail) {
  zx::channel channel_local, channel_remote;

  ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, channel_local.write_etc(0, nullptr, 10, nullptr, 0));

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, channel_local.write_etc(0, nullptr, 0, nullptr, 10));
}

TEST(ChannelWriteEtcTest, RemoveAllHandleRightsShouldSucceed) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result) {
    zx::channel channel_local, channel_remote;
    zx::socket socket_local, socket_remote;

    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

    zx_handle_t socket_local_handle = socket_local.release();

    zx_handle_disposition_t send_handle_list[] = {
        {op, socket_local_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_NONE, ZX_OK}};

    ASSERT_OK(channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                      countof(send_handle_list)),
              "%s", test_case_str[op].c_str());

    ASSERT_EQ(expected_close_result, zx_handle_close(socket_local_handle), "%s",
              test_case_str[op].c_str());

    uint32_t incoming_bytes;
    zx_handle_t incoming_handle;
    zx_info_handle_basic_t info;
    ASSERT_OK(channel_remote.read(0, &incoming_bytes, &incoming_handle, sizeof(incoming_bytes), 1,
                                  nullptr, nullptr));
    GetBasicInfo(incoming_handle, &info);

    ASSERT_EQ(ZX_RIGHT_NONE, info.rights);
    ASSERT_OK(zx_handle_close(incoming_handle));
    ASSERT_EQ(ZX_OBJ_TYPE_SOCKET, info.type);
  };

  TestHelper(ZX_HANDLE_OP_MOVE, ZX_ERR_BAD_HANDLE);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_OK);
}

TEST(ChannelWriteEtcTest, RemovingSomeHandleRightsShouldSucceed) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result) {
    zx::channel channel_local, channel_remote;
    zx::socket socket_local, socket_remote;

    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

    zx_handle_t socket_local_handle = socket_local.release();

    zx_info_handle_basic_t info_before;
    GetBasicInfo(socket_local_handle, &info_before);

    zx_handle_disposition_t send_handle_list[] = {
        {op, socket_local_handle, ZX_OBJ_TYPE_NONE, info_before.rights & ~ZX_RIGHT_WRITE, ZX_OK}};

    ASSERT_OK(channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                      countof(send_handle_list)),
              "%s", test_case_str[op].c_str());

    uint32_t incoming_bytes;
    zx_handle_t incoming_handle;
    zx_info_handle_basic_t info_after;
    ASSERT_OK(channel_remote.read(0, &incoming_bytes, &incoming_handle, sizeof(incoming_bytes), 1,
                                  nullptr, nullptr));
    GetBasicInfo(incoming_handle, &info_after);

    EXPECT_EQ(info_before.rights & ~ZX_RIGHT_WRITE, info_after.rights, "%s",
              test_case_str[op].c_str());
    EXPECT_OK(zx_handle_close(incoming_handle), "%s", test_case_str[op].c_str());
    EXPECT_EQ(ZX_OBJ_TYPE_SOCKET, info_after.type, "%s", test_case_str[op].c_str());

    // In ZX_HANDLE_OP_DUPLICATE case, original right should not have been affected
    if (op == ZX_HANDLE_OP_DUPLICATE) {
      zx_info_handle_basic_t info_original_after;
      GetBasicInfo(socket_local_handle, &info_original_after);
      EXPECT_EQ(info_before.rights, info_original_after.rights, "%s", test_case_str[op].c_str());
    }

    ASSERT_EQ(expected_close_result, zx_handle_close(socket_local_handle), "%s",
              test_case_str[op].c_str());
  };

  TestHelper(ZX_HANDLE_OP_MOVE, ZX_ERR_BAD_HANDLE);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_OK);
}

TEST(ChannelWriteEtcTest, SameHandleRightsBitsShouldSucceed) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result) {
    zx::channel channel_local, channel_remote;
    zx::socket socket_local, socket_remote;

    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

    zx_handle_t socket_local_handle = socket_local.release();

    zx_info_handle_basic_t info_before;
    GetBasicInfo(socket_local_handle, &info_before);

    zx_handle_disposition_t send_handle_list[] = {
        {op, socket_local_handle, ZX_OBJ_TYPE_NONE, info_before.rights, ZX_OK}};

    ASSERT_OK(channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                      countof(send_handle_list)),
              "%s", test_case_str[op].c_str());

    ASSERT_EQ(expected_close_result, zx_handle_close(socket_local_handle), "%s",
              test_case_str[op].c_str());

    uint32_t incoming_bytes;
    zx_handle_t incoming_handle;
    zx_info_handle_basic_t info_after;
    ASSERT_OK(channel_remote.read(0, &incoming_bytes, &incoming_handle, sizeof(incoming_bytes), 1,
                                  nullptr, nullptr));
    GetBasicInfo(incoming_handle, &info_after);

    EXPECT_EQ(info_before.rights, info_after.rights, "%s", test_case_str[op].c_str());
    EXPECT_OK(zx_handle_close(incoming_handle), "%s", test_case_str[op].c_str());
    EXPECT_EQ(ZX_OBJ_TYPE_SOCKET, info_after.type, "%s", test_case_str[op].c_str());
  };

  TestHelper(ZX_HANDLE_OP_MOVE, ZX_ERR_BAD_HANDLE);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_OK);
}

TEST(ChannelWriteEtcTest, SameHandleRightsFlagShouldSucceed) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result) {
    zx::channel channel_local, channel_remote;
    zx::socket socket_local, socket_remote;

    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

    zx_handle_t socket_local_handle = socket_local.release();

    zx_info_handle_basic_t info_before;
    GetBasicInfo(socket_local_handle, &info_before);

    zx_handle_disposition_t send_handle_list[] = {
        {op, socket_local_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

    ASSERT_OK(channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                      countof(send_handle_list)),
              "%s", test_case_str[op].c_str());

    ASSERT_EQ(expected_close_result, zx_handle_close(socket_local_handle), "%s",
              test_case_str[op].c_str());

    uint32_t incoming_bytes;
    zx_handle_t incoming_handle;
    zx_info_handle_basic_t info_after;
    ASSERT_OK(channel_remote.read(0, &incoming_bytes, &incoming_handle, sizeof(incoming_bytes), 1,
                                  nullptr, nullptr));
    GetBasicInfo(incoming_handle, &info_after);

    EXPECT_EQ(info_before.rights, info_after.rights, "%s", test_case_str[op].c_str());
    EXPECT_OK(zx_handle_close(incoming_handle), "%s", test_case_str[op].c_str());
    EXPECT_EQ(ZX_OBJ_TYPE_SOCKET, info_after.type, "%s", test_case_str[op].c_str());
  };

  TestHelper(ZX_HANDLE_OP_MOVE, ZX_ERR_BAD_HANDLE);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_OK);
}

TEST(ChannelWriteEtcTest, HandleWithoutDuplicateRightsMoveOpSucceedsDuplicateOpFails) {
  auto TestHelper = [](zx_handle_op_t op, zx_status_t expected_close_result,
                       zx_status_t expected_write_result) {
    zx::channel channel_local, channel_remote;
    zx::socket socket_local, socket_remote;

    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_local, &socket_remote));
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));

    zx_handle_t socket_local_handle = socket_local.release();
    zx_info_handle_basic_t info;
    GetBasicInfo(socket_local_handle, &info);

    zx_handle_t socket_local_no_duplicate_handle;
    ASSERT_OK(zx_handle_duplicate(socket_local_handle, info.rights & ~ZX_RIGHT_DUPLICATE,
                                  &socket_local_no_duplicate_handle));

    zx_handle_disposition_t send_handle_list[] = {
        {op, socket_local_no_duplicate_handle, ZX_OBJ_TYPE_NONE, ZX_RIGHT_SAME_RIGHTS, ZX_OK}};

    EXPECT_EQ(expected_write_result,
              channel_local.write_etc(0, &kChannelData, sizeof(kChannelData), send_handle_list,
                                      countof(send_handle_list)),
              "%s rights: 0x%x", test_case_str[op].c_str(), info.rights & ~ZX_RIGHT_DUPLICATE);

    EXPECT_EQ(expected_close_result, zx_handle_close(socket_local_no_duplicate_handle), "%s",
              test_case_str[op].c_str());
  };

  TestHelper(ZX_HANDLE_OP_MOVE, ZX_ERR_BAD_HANDLE, ZX_OK);
  TestHelper(ZX_HANDLE_OP_DUPLICATE, ZX_OK, ZX_ERR_ACCESS_DENIED);
}

}  // namespace
