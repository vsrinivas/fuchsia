// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/txn_header.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <fidl/test/echo/c/fidl.h>
#include <zxtest/zxtest.h>

static int kContext = 42;
static size_t g_echo_call_count = 0u;

static zx_status_t echo(void* ctx, zx_handle_t process, zx_handle_t thread, fidl_txn_t* txn) {
  ++g_echo_call_count;
  EXPECT_EQ(&kContext, ctx, "");
  EXPECT_NE(ZX_HANDLE_INVALID, process, "");
  EXPECT_NE(ZX_HANDLE_INVALID, thread, "");
  EXPECT_NE(NULL, txn, "");
  zx_handle_close(process);
  zx_handle_close(thread);
  return ZX_OK;
}

TEST(ServerTests, dispatch_test) {
  fidl_test_echo_Echo_ops_t ops = {
      .Echo = echo,
  };

  fidl_test_echo_EchoEchoRequest request;
  memset(&request, 0, sizeof(request));
  zx_txid_t txid = 42;
  fidl_init_txn_header(&request.hdr, txid, fidl_test_echo_EchoEchoOrdinal);
  request.process = FIDL_HANDLE_PRESENT;
  request.thread = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[2];
  fidl_incoming_msg_t msg = {
      .bytes = &request,
      .handles = handles,
      .num_bytes = sizeof(request),
      .num_handles = 2,
  };

  fidl_txn_t txn;
  memset(&txn, 0, sizeof(txn));

  // Success

  zx_status_t status = zx_eventpair_create(0, &handles[0], &handles[1]);
  ASSERT_EQ(ZX_OK, status, "");
  EXPECT_EQ(0u, g_echo_call_count, "");
  status = fidl_test_echo_Echo_dispatch(&kContext, &txn, &msg, &ops);
  ASSERT_EQ(ZX_OK, status, "");
  EXPECT_EQ(1u, g_echo_call_count, "");
  g_echo_call_count = 0u;

  // Bad ordinal (dispatch)

  request.hdr.ordinal = 8949;
  zx_handle_t canary0 = ZX_HANDLE_INVALID;
  status = zx_eventpair_create(0, &handles[0], &canary0);
  ASSERT_EQ(ZX_OK, status, "");

  zx_handle_t canary1 = ZX_HANDLE_INVALID;
  status = zx_eventpair_create(0, &handles[1], &canary1);
  ASSERT_EQ(ZX_OK, status, "");

  EXPECT_EQ(0u, g_echo_call_count, "");
  status = fidl_test_echo_Echo_dispatch(&kContext, &txn, &msg, &ops);
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, status, "");
  EXPECT_EQ(0u, g_echo_call_count, "");
  g_echo_call_count = 0u;
  status = zx_object_signal_peer(canary0, 0, ZX_USER_SIGNAL_0);
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, status, "");
  status = zx_object_signal_peer(canary1, 0, ZX_USER_SIGNAL_0);
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, status, "");
  zx_handle_close(canary0);
  zx_handle_close(canary1);

  // Bad ordinal (try_dispatch)

  request.hdr.ordinal = 8949;
  canary0 = ZX_HANDLE_INVALID;
  status = zx_eventpair_create(0, &handles[0], &canary0);
  ASSERT_EQ(ZX_OK, status, "");

  canary1 = ZX_HANDLE_INVALID;
  status = zx_eventpair_create(0, &handles[1], &canary1);
  ASSERT_EQ(ZX_OK, status, "");

  EXPECT_EQ(0u, g_echo_call_count, "");
  status = fidl_test_echo_Echo_try_dispatch(&kContext, &txn, &msg, &ops);
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, status, "");
  EXPECT_EQ(0u, g_echo_call_count, "");
  g_echo_call_count = 0u;
  status = zx_object_signal_peer(canary0, 0, ZX_USER_SIGNAL_0);
  ASSERT_EQ(ZX_OK, status, "");
  status = zx_object_signal_peer(canary1, 0, ZX_USER_SIGNAL_0);
  ASSERT_EQ(ZX_OK, status, "");
  zx_handle_close_many(handles, 2);
  zx_handle_close(canary0);
  zx_handle_close(canary1);
}

typedef struct my_connection {
  fidl_txn_t txn;
  size_t count;
} my_connection_t;

static zx_status_t reply_handler(fidl_txn_t* txn, const fidl_outgoing_msg_t* msg) {
  my_connection_t* my_txn = (my_connection_t*)txn;
  EXPECT_EQ(sizeof(fidl_test_echo_EchoEchoResponse), msg->num_bytes, "");
  EXPECT_EQ(0u, msg->num_handles, "");

  fidl_message_header_t* hdr = (fidl_message_header_t*)msg->bytes;
  EXPECT_EQ(hdr->magic_number, kFidlWireFormatMagicNumberInitial, "");
  ++my_txn->count;
  return ZX_OK;
}

TEST(ServerTests, reply_test) {
  my_connection_t conn;
  conn.txn.reply = reply_handler;
  conn.count = 0u;

  zx_status_t status = fidl_test_echo_EchoEcho_reply(&conn.txn, ZX_OK);
  ASSERT_EQ(ZX_OK, status, "");
  EXPECT_EQ(1u, conn.count, "");
}

static zx_status_t return_async(void* ctx, zx_handle_t process, zx_handle_t thread,
                                fidl_txn_t* txn) {
  zx_handle_close(process);
  zx_handle_close(thread);
  return ZX_ERR_ASYNC;
}

TEST(ServerTests, error_test) {
  fidl_test_echo_Echo_ops_t ops = {
      .Echo = return_async,
  };

  fidl_test_echo_EchoEchoRequest request;
  memset(&request, 0, sizeof(request));
  zx_txid_t txid = 42;
  fidl_init_txn_header(&request.hdr, txid, fidl_test_echo_EchoEchoOrdinal);
  request.process = FIDL_HANDLE_PRESENT;
  request.thread = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[2];
  fidl_incoming_msg_t msg = {
      .bytes = &request,
      .handles = handles,
      .num_bytes = sizeof(request),
      .num_handles = 2,
  };

  fidl_txn_t txn;
  memset(&txn, 0, sizeof(txn));

  zx_status_t status = zx_eventpair_create(0, &handles[0], &handles[1]);
  ASSERT_EQ(ZX_OK, status, "");
  status = fidl_test_echo_Echo_try_dispatch(NULL, &txn, &msg, &ops);
  ASSERT_EQ(ZX_ERR_ASYNC, status, "");
}

TEST(ServerTests, incompatible_magic_test) {
  fidl_test_echo_Echo_ops_t ops = {
      .Echo = return_async,
  };

  fidl_test_echo_EchoEchoRequest request;
  memset(&request, 0, sizeof(request));
  zx_txid_t txid = 42;
  fidl_init_txn_header(&request.hdr, txid, fidl_test_echo_EchoEchoOrdinal);
  request.hdr.magic_number = 0;
  request.process = FIDL_HANDLE_PRESENT;
  request.thread = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[2];
  fidl_incoming_msg_t msg = {
      .bytes = &request,
      .handles = handles,
      .num_bytes = sizeof(request),
      .num_handles = 2,
  };

  fidl_txn_t txn;
  memset(&txn, 0, sizeof(txn));

  zx_status_t status = zx_eventpair_create(0, &handles[0], &handles[1]);
  ASSERT_EQ(ZX_OK, status, "");
  status = fidl_test_echo_Echo_try_dispatch(NULL, &txn, &msg, &ops);
  ASSERT_EQ(ZX_ERR_PROTOCOL_NOT_SUPPORTED, status, "");
}
