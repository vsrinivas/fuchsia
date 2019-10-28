// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <threads.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <fidl/test/echo/c/fidl.h>
#include <fidl/test/ctransformer/c/fidl.h>
#include <lib/fidl/txn_header.h>
#include <unittest/unittest.h>

static int test_server(void* ctx) {
  zx_handle_t server = *(zx_handle_t*)ctx;
  zx_status_t status = ZX_OK;

  while (status == ZX_OK) {
    zx_signals_t observed;
    status = zx_object_wait_one(server, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                ZX_TIME_INFINITE, &observed);
    if ((observed & ZX_CHANNEL_READABLE) == 0) {
      break;
    }
    ASSERT_EQ(ZX_OK, status, "");
    char msg[1024];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t actual_bytes = 0u;
    uint32_t actual_handles = 0u;
    status = zx_channel_read(server, 0, msg, handles, sizeof(msg), ZX_CHANNEL_MAX_MSG_HANDLES,
                             &actual_bytes, &actual_handles);
    ASSERT_EQ(ZX_OK, status, "");
    ASSERT_GE(actual_bytes, sizeof(fidl_message_header_t), "");
    ASSERT_EQ(actual_handles, 0u, "");
    fidl_message_header_t* req = (fidl_message_header_t*)msg;

    // Respond with the v1 version of |example/Sandwich4|.
    // This excerpt of bytes is taken directly from zircon/system/utest/fidl/transformer_tests.cc.
    uint8_t sandwich4_case1_v1[] = {
      0x01, 0x02, 0x03, 0x04,  // Sandwich4.before
      0x00, 0x00, 0x00, 0x00,  // Sandwich4.before (padding)

      0x19, 0x10, 0x41, 0x5e,  // UnionSize36Alignment4.tag, i.e. Sandwich4.the_union
      0x00, 0x00, 0x00, 0x00,  // UnionSize36Alignment4.tag (padding)
      0x32, 0x00, 0x00, 0x00,  // UnionSize36Alignment4.env.num_bytes
      0x00, 0x00, 0x00, 0x00,  // UnionSize36Alignment4.env.num_handle
      0xff, 0xff, 0xff, 0xff,  // UnionSize36Alignment4.env.presence
      0xff, 0xff, 0xff, 0xff,  // UnionSize36Alignment4.env.presence [cont.]

      0x05, 0x06, 0x07, 0x08,  // Sandwich4.after
      0x00, 0x00, 0x00, 0x00,  // Sandwich4.after (padding)

      0xa0, 0xa1, 0xa2, 0xa3,  // UnionSize36Alignment4.data, i.e. Sandwich4.the_union.data
      0xa4, 0xa5, 0xa6, 0xa7,  // UnionSize36Alignment4.data [cont.]
      0xa8, 0xa9, 0xaa, 0xab,  // UnionSize36Alignment4.data [cont.]
      0xac, 0xad, 0xae, 0xaf,  // UnionSize36Alignment4.data [cont.]
      0xb0, 0xb1, 0xb2, 0xb3,  // UnionSize36Alignment4.data [cont.]
      0xb4, 0xb5, 0xb6, 0xb7,  // UnionSize36Alignment4.data [cont.]
      0xb8, 0xb9, 0xba, 0xbb,  // UnionSize36Alignment4.data [cont.]
      0xbc, 0xbd, 0xbe, 0xbf,  // UnionSize36Alignment4.data [cont.]
    };
    uint8_t response[sizeof(fidl_message_header_t) + sizeof(sandwich4_case1_v1)] = {};
    fidl_message_header_t* response_hdr = (fidl_message_header_t*)response;
    fidl_init_txn_header(response_hdr, req->txid, req->ordinal);
    // Set the flag indicating unions are encoded as xunions.
    response_hdr->flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
    memcpy(&response[sizeof(fidl_message_header_t)], sandwich4_case1_v1,
           sizeof(sandwich4_case1_v1));

    status = zx_channel_write(server, 0, response, sizeof(response), NULL, 0);
    ASSERT_EQ(ZX_OK, status, "");
  }

  zx_handle_close(server);
  return 0;
}

static bool xunion_to_union(void) {
  BEGIN_TEST;

  zx_handle_t client, server;
  zx_status_t status = zx_channel_create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "");

  thrd_t thread;
  int rv = thrd_create(&thread, test_server, &server);
  ASSERT_EQ(thrd_success, rv, "");

  // Server is responding in v1 wire-format, but we should be able to receive it
  // as the old wire-format.
  example_Sandwich4 sandwich4;
  status = fidl_test_ctransformer_TestReceiveUnion(client, &sandwich4);
  EXPECT_EQ(ZX_OK, status, "");

  EXPECT_EQ(example_UnionSize36Alignment4Tag_variant, sandwich4.the_union.tag, "");
  EXPECT_EQ(0x04030201, sandwich4.before, "");
  EXPECT_EQ(0x08070605, sandwich4.after, "");

  status = zx_handle_close(client);
  ASSERT_EQ(ZX_OK, status, "");

  int result = 0;
  rv = thrd_join(thread, &result);
  ASSERT_EQ(thrd_success, rv, "");

  END_TEST;
}

BEGIN_TEST_CASE(c_transformer_smoke_tests)
RUN_NAMED_TEST("Test xunion -> union transformer integration", xunion_to_union)
END_TEST_CASE(c_transformer_smoke_tests);
