// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/txn_header.h>
#include <string.h>
#include <threads.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <fidl/test/echo/c/fidl.h>
#include <zxtest/zxtest.h>

static void echo_server(zx_handle_t server) {
  zx_status_t status = ZX_OK;

  while (status == ZX_OK) {
    zx_signals_t observed;
    status = zx_object_wait_one(server, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                ZX_TIME_INFINITE, &observed);
    if ((observed & ZX_CHANNEL_READABLE) != 0) {
      ASSERT_EQ(ZX_OK, status, "");
      char msg[1024];
      zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
      uint32_t actual_bytes = 0u;
      uint32_t actual_handles = 0u;
      status = zx_channel_read(server, 0, msg, handles, sizeof(msg), ZX_CHANNEL_MAX_MSG_HANDLES,
                               &actual_bytes, &actual_handles);
      ASSERT_EQ(ZX_OK, status, "");
      ASSERT_GE(actual_bytes, sizeof(fidl_message_header_t), "");
      ASSERT_EQ(actual_handles, 2u, "");
      zx_handle_close_many(handles, actual_handles);
      fidl_message_header_t* req = (fidl_message_header_t*)msg;
      fidl_test_echo_EchoEchoResponse response;
      memset(&response, 0, sizeof(response));
      fidl_init_txn_header(&response.hdr, req->txid, req->ordinal);
      response.status = ZX_OK;
      status = zx_channel_write(server, 0, &response, sizeof(response), NULL, 0);
      ASSERT_EQ(ZX_OK, status, "");
    } else {
      break;
    }
  }

  zx_handle_close(server);
}

static int echo_server_wrapper(void* ctx) {
  echo_server(*(zx_handle_t*)ctx);
  return 0;
}

TEST(ClientTests, echo_test) {
  zx_handle_t client, server;
  zx_status_t status = zx_channel_create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "");

  thrd_t thread;
  int rv = thrd_create(&thread, echo_server_wrapper, &server);
  ASSERT_EQ(thrd_success, rv, "");

  zx_handle_t h0, h1;
  status = zx_eventpair_create(0, &h0, &h1);
  ASSERT_EQ(ZX_OK, status, "");

  zx_status_t application_status;
  status = fidl_test_echo_EchoEcho(client, h0, h1, &application_status);
  ASSERT_EQ(ZX_OK, status, "");
  ASSERT_EQ(ZX_OK, application_status, "");

  status = zx_handle_close(client);
  ASSERT_EQ(ZX_OK, status, "");

  int result = 0;
  rv = thrd_join(thread, &result);
  ASSERT_EQ(thrd_success, rv, "");
}

TEST(ClientTests, magic_number_request_test) {
  zx_handle_t client, server;
  zx_status_t status = zx_channel_create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "");

  status = fidl_test_echo_EchoPing(client);
  ASSERT_EQ(ZX_OK, status, "");

  char bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl_incoming_msg_t msg = {
      .bytes = bytes,
      .handles = handles,
      .num_bytes = 0u,
      .num_handles = 0u,
  };
  status = zx_channel_read(server, 0, bytes, handles, ZX_CHANNEL_MAX_MSG_BYTES,
                           ZX_CHANNEL_MAX_MSG_HANDLES, &msg.num_bytes, &msg.num_handles);
  ASSERT_EQ(ZX_OK, status, "");
  ASSERT_EQ(msg.num_bytes, sizeof(fidl_message_header_t), "");

  fidl_message_header_t* hdr = (fidl_message_header_t*)msg.bytes;
  ASSERT_EQ(hdr->magic_number, kFidlWireFormatMagicNumberInitial, "");
}
