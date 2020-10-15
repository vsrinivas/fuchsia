// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ldsvc/c/fidl.h>
#include <lib/fidl/coding.h>
#include <string.h>
#include <threads.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <ldmsg/ldmsg.h>
#include <zxtest/zxtest.h>

static bool g_server_done = false;

static zx_status_t ldsvc_Done(void* ctx) {
  g_server_done = true;
  return ZX_OK;
}

static zx_status_t ldsvc_LoadObject(void* ctx, const char* object_name_data,
                                    size_t object_name_size, fidl_txn_t* txn) {
  size_t len = strlen("object name");
  EXPECT_EQ(len, object_name_size, "");
  EXPECT_EQ(0, memcmp(object_name_data, "object name", len), "");
  zx_handle_t event = ZX_HANDLE_INVALID;
  EXPECT_EQ(ZX_OK, zx_event_create(0, &event), "");
  return fuchsia_ldsvc_LoaderLoadObject_reply(txn, 42, event);
}

static zx_status_t ldsvc_Config(void* ctx, const char* config_data, size_t config_size,
                                fidl_txn_t* txn) {
  size_t len = strlen("my config");
  EXPECT_EQ(len, config_size, "");
  EXPECT_EQ(0, memcmp(config_data, "my config", len), "");
  return fuchsia_ldsvc_LoaderConfig_reply(txn, 44);
}

static zx_status_t ldsvc_Clone(void* ctx, zx_handle_t loader, fidl_txn_t* txn) {
  EXPECT_EQ(ZX_OK, zx_handle_close(loader), "");
  return fuchsia_ldsvc_LoaderClone_reply(txn, 45);
}

static const fuchsia_ldsvc_Loader_ops_t kOps = {
    .Done = ldsvc_Done,
    .LoadObject = ldsvc_LoadObject,
    .Config = ldsvc_Config,
    .Clone = ldsvc_Clone,
};

typedef struct ldsvc_connection {
  fidl_txn_t txn;
  zx_handle_t channel;
  zx_txid_t txid;
  uint32_t reply_count;
} ldsvc_connection_t;

static zx_status_t ldsvc_server_reply(fidl_txn_t* txn, const fidl_outgoing_msg_t* msg) {
  ldsvc_connection_t* conn = (ldsvc_connection_t*)txn;
  if (msg->num_bytes < sizeof(fidl_message_header_t))
    return ZX_ERR_INVALID_ARGS;
  fidl_message_header_t* hdr = (fidl_message_header_t*)msg->bytes;
  hdr->txid = conn->txid;
  conn->txid = 0;
  ++conn->reply_count;
  return zx_channel_write(conn->channel, 0, msg->bytes, msg->num_bytes, msg->handles,
                          msg->num_handles);
}

static void ldsvc_server(zx_handle_t channel_handle) {
  ldsvc_connection_t conn = {
      .txn.reply = ldsvc_server_reply,
      .channel = channel_handle,
      .reply_count = 0u,
  };
  zx_status_t status = ZX_OK;
  g_server_done = false;

  while (status == ZX_OK && !g_server_done) {
    zx_signals_t observed;
    status = zx_object_wait_one(conn.channel, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                ZX_TIME_INFINITE, &observed);
    if ((observed & ZX_CHANNEL_READABLE) != 0) {
      ASSERT_EQ(ZX_OK, status, "");
      char bytes[ZX_CHANNEL_MAX_MSG_BYTES];
      zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
      fidl_incoming_msg_t msg = {
          .bytes = bytes,
          .handles = handles,
          .num_bytes = 0u,
          .num_handles = 0u,
      };
      status = zx_channel_read(conn.channel, 0, bytes, handles, ZX_CHANNEL_MAX_MSG_BYTES,
                               ZX_CHANNEL_MAX_MSG_HANDLES, &msg.num_bytes, &msg.num_handles);
      ASSERT_EQ(ZX_OK, status, "");
      ASSERT_GE(msg.num_bytes, sizeof(fidl_message_header_t), "");
      fidl_message_header_t* hdr = (fidl_message_header_t*)msg.bytes;
      conn.txid = hdr->txid;
      conn.reply_count = 0u;
      status = fuchsia_ldsvc_Loader_dispatch(NULL, &conn.txn, &msg, &kOps);
      ASSERT_EQ(ZX_OK, status, "");
      if (!g_server_done)
        ASSERT_EQ(1u, conn.reply_count, "");
    } else {
      break;
    }
  }

  zx_handle_close(conn.channel);
}

static int ldsvc_server_wrapper(void* ctx) {
  ldsvc_server(*(zx_handle_t*)ctx);
  return 0;
}

TEST(LdsvcTests, loader_test) {
  zx_handle_t client, server;
  zx_status_t status = zx_channel_create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "");

  thrd_t thread;
  int rv = thrd_create(&thread, ldsvc_server_wrapper, &server);
  ASSERT_EQ(thrd_success, rv, "");

  {
    const char* object_name = "object name";
    zx_status_t rv = ZX_OK;
    zx_handle_t object = ZX_HANDLE_INVALID;
    ASSERT_EQ(
        ZX_OK,
        fuchsia_ldsvc_LoaderLoadObject(client, object_name, strlen(object_name), &rv, &object), "");
    ASSERT_EQ(42, rv, "");
    ASSERT_EQ(ZX_OK, zx_handle_close(object), "");
  }

  {
    const char* config = "my config";
    zx_status_t rv = ZX_OK;
    ASSERT_EQ(ZX_OK, fuchsia_ldsvc_LoaderConfig(client, config, strlen(config), &rv), "");
    ASSERT_EQ(44, rv, "");
  }

  {
    zx_status_t rv = ZX_OK;
    zx_handle_t h1, h2;
    ASSERT_EQ(ZX_OK, zx_eventpair_create(0, &h1, &h2), "");
    ASSERT_EQ(ZX_OK, fuchsia_ldsvc_LoaderClone(client, h1, &rv), "");
    ASSERT_EQ(45, rv, "");
    ASSERT_EQ(ZX_ERR_PEER_CLOSED, zx_object_signal_peer(h2, 0, 0), "");
    ASSERT_EQ(ZX_OK, zx_handle_close(h2), "");
  }

  ASSERT_EQ(ZX_OK, fuchsia_ldsvc_LoaderDone(client), "");
  ASSERT_EQ(ZX_OK, zx_handle_close(client), "");

  int result = 0;
  rv = thrd_join(thread, &result);
  ASSERT_EQ(thrd_success, rv, "");
}

// This doesn't really need to be a separate test.  But for documentation: we
// hardcode the ordinals in ldmsg.h.  They need to be the same as the generated
// ordinals.
//
// If you need to make a change in how ordinals are calculated, first change
// GenOrdinal, then change LDMSG_*, and then change Ordinal.
TEST(LdsvcTests, ordinals_are_consistent) {
  static_assert(LDMSG_OP_DONE == fuchsia_ldsvc_LoaderDoneOrdinal, "Done ordinals need to match");
  static_assert(LDMSG_OP_LOAD_OBJECT == fuchsia_ldsvc_LoaderLoadObjectOrdinal,
                "LoadObject ordinals need to match");
  static_assert(LDMSG_OP_CONFIG == fuchsia_ldsvc_LoaderConfigOrdinal,
                "Config ordinals need to match");
  static_assert(LDMSG_OP_CLONE == fuchsia_ldsvc_LoaderCloneOrdinal, "Clone ordinals need to match");
}

// Assumes that the ordinal_value is an interface method that takes a single
// string.  Encodes some data with the ldmsg encoder, and then decodes it with
// the fidl decoder.  Then, encodes some data with the fidl encoder, and decodes
// it with the ldmsg decoder.
static void check_string_round_trip(uint64_t ordinal_value, const fidl_type_t* table) {
  ldmsg_req_t req;
  memset(&req, 0xba, sizeof(req));
  memset(&req.header, 0, sizeof(req.header));
  size_t req_len_out;
  req.header.ordinal = ordinal_value;
  const char* data = "libfdio.so";
  ldmsg_req_encode(&req, &req_len_out, data, strlen(data));
  EXPECT_EQ((uintptr_t)req.common.string.data, FIDL_ALLOC_PRESENT, "");
  const char* err_msg = NULL;
  zx_status_t res = fidl_decode(table, (void*)&req, (uint32_t)req_len_out, NULL, 0, &err_msg);
  EXPECT_EQ(ZX_OK, res, "result of fidl_decode incorrect");
  EXPECT_EQ(0, strcmp(req.common.string.data, data), "data not decoded correctly");
  EXPECT_EQ(err_msg, NULL, "%s", err_msg);
  uint32_t out_actual_handles;
  res = fidl_encode(table, (void*)&req, (uint32_t)req_len_out, NULL, 0, &out_actual_handles,
                    &err_msg);
  EXPECT_EQ(ZX_OK, res, "Encoding failure");
  EXPECT_EQ(err_msg, NULL, "%s", err_msg);
  size_t len_out;
  const char* data_out;
  ldmsg_req_decode(&req, req_len_out, &data_out, &len_out);
  EXPECT_EQ(0, strcmp(data_out, data), "data from decoder not correct value");
  EXPECT_EQ(len_out, strlen(data), "len from decoder not correct length");
}

// Checks that the ldmsg encoder and decoder behave consistently with the C
// binding's default encoder and decoder.
TEST(LdsvcTests, ldmsg_functions_are_consistent) {
  {
    ldmsg_req_t done_req;
    memset(&done_req, 0xba, sizeof(done_req));
    memset(&done_req.header, 0, sizeof(done_req.header));
    size_t req_len_out;
    done_req.header.ordinal = fuchsia_ldsvc_LoaderDoneOrdinal;
    ldmsg_req_encode(&done_req, &req_len_out, NULL, 0);
    const char* err_msg = NULL;
    zx_status_t res = fidl_decode(&fuchsia_ldsvc_LoaderDoneRequestTable, (void*)&done_req,
                                  (uint32_t)req_len_out, NULL, 0, &err_msg);
    EXPECT_EQ(ZX_OK, res, "fidl_decode return value not ZX_OK");
    EXPECT_EQ(err_msg, NULL, "%s", err_msg);
    // Don't bother with the round-trip here because there is no data to
    // encode.
  }

  check_string_round_trip(fuchsia_ldsvc_LoaderLoadObjectOrdinal,
                          &fuchsia_ldsvc_LoaderLoadObjectRequestTable);
  check_string_round_trip(fuchsia_ldsvc_LoaderConfigOrdinal,
                          &fuchsia_ldsvc_LoaderConfigRequestTable);
}

static zx_status_t validate_reply(fidl_txn_t* txn, const fidl_outgoing_msg_t* msg) {
  EXPECT_EQ(msg->num_bytes, ldmsg_rsp_get_size((ldmsg_rsp_t*)msg->bytes), "");
  return ZX_OK;
}

TEST(LdsvcTests, replies_are_consistent) {
  fidl_txn_t txn = {
      .reply = validate_reply,
  };

  zx_handle_t event = ZX_HANDLE_INVALID;
  ASSERT_EQ(ZX_OK, zx_event_create(0, &event), "");

  ASSERT_EQ(ZX_OK, fuchsia_ldsvc_LoaderLoadObject_reply(&txn, 42, event), "");
  ASSERT_EQ(ZX_OK, fuchsia_ldsvc_LoaderConfig_reply(&txn, 44), "");
  ASSERT_EQ(ZX_OK, fuchsia_ldsvc_LoaderClone_reply(&txn, 45), "");

  zx_handle_close(event);
}
