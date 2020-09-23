// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>
#include <thread>

// Needed to test API coverage of null params in GCC.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
#include <lib/zx/channel.h>
#pragma GCC diagnostic pop

#include <zxtest/zxtest.h>

namespace {

template <size_t NBufBytes = 65536, size_t NBufHandles = 64>
class EchoServer {
 public:
  EchoServer() {
    zx::channel server_end;
    ASSERT_OK(zx::channel::create(0, &client_end, &server_end));
    thread = std::thread(ServerThread, std::move(server_end));
  }

  ~EchoServer() { thread.join(); }

  zx::channel ClientEnd() { return std::move(client_end); }

 private:
  static void ServerThread(zx::channel server_end) {
    uint32_t actual_bytes;
    uint32_t actual_handles;
    uint8_t bytes[NBufBytes];
    zx_handle_t handles[NBufHandles];
    ASSERT_EQ(ZX_OK, server_end.read(0, bytes, handles, NBufBytes, NBufHandles, &actual_bytes,
                                     &actual_handles));
    ASSERT_EQ(ZX_OK, server_end.write(0, bytes, actual_bytes, handles, actual_handles));
  }

  zx::channel client_end;
  std::thread thread;
};

TEST(ChannelCallEtcTest, BytesOnlySuccessCase) {
  EchoServer echo_server;
  zx::channel client_end = echo_server.ClientEnd();

  constexpr size_t message_size = 512;
  uint8_t request_bytes[message_size];
  uint8_t response_bytes[message_size];

  for (size_t i = 0; i < message_size; i++) {
    request_bytes[i] = static_cast<uint8_t>(i % 256);
  }

  zx_channel_call_etc_args_t args = {
      .wr_bytes = request_bytes,
      .wr_handles = nullptr,
      .rd_bytes = response_bytes,
      .rd_handles = nullptr,
      .wr_num_bytes = message_size,
      .wr_num_handles = 0,
      .rd_num_bytes = message_size,
      .rd_num_handles = 0,
  };
  uint32_t actual_bytes = 0;
  uint32_t actual_handles = 0;
  ASSERT_EQ(ZX_OK,
            client_end.call_etc(0, zx::time::infinite(), &args, &actual_bytes, &actual_handles));
  EXPECT_EQ(message_size, actual_bytes);
  EXPECT_EQ(0, actual_handles);
  // The first four bytes are overwritten by zx_channel_call with the
  // txid.
  EXPECT_BYTES_EQ(request_bytes + 4, response_bytes + 4, message_size - 4);
}

TEST(ChannelCallEtcTest, HandlesSuccessCase) {
  EchoServer echo_server;
  zx::channel client_end = echo_server.ClientEnd();

  constexpr size_t message_size = 4;
  uint8_t request_bytes[message_size];
  uint8_t response_bytes[message_size];

  zx::port port0;
  zx::port port1;
  ASSERT_EQ(ZX_OK, zx::port::create(0, &port0));
  ASSERT_EQ(ZX_OK, zx::port::create(0, &port1));

  zx_info_handle_basic_t info0;
  zx_info_handle_basic_t info1;
  ASSERT_EQ(ZX_OK, zx_object_get_info(port0.get(), ZX_INFO_HANDLE_BASIC, &info0, sizeof(info0),
                                      nullptr, nullptr));
  ASSERT_EQ(ZX_OK, zx_object_get_info(port1.get(), ZX_INFO_HANDLE_BASIC, &info1, sizeof(info1),
                                      nullptr, nullptr));

  constexpr size_t handles_size = 2;
  zx_handle_disposition_t request_handles[handles_size] = {
      {
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = port0.release(),
          .type = ZX_OBJ_TYPE_NONE,
          .rights = ZX_RIGHT_SAME_RIGHTS,
          .result = ZX_OK,
      },
      {
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = port1.release(),
          .type = ZX_OBJ_TYPE_NONE,
          .rights = ZX_RIGHT_SAME_RIGHTS,
          .result = ZX_OK,
      },
  };
  zx_handle_info_t response_handles[handles_size] = {};

  zx_channel_call_etc_args_t args = {
      .wr_bytes = request_bytes,
      .wr_handles = request_handles,
      .rd_bytes = response_bytes,
      .rd_handles = response_handles,
      .wr_num_bytes = message_size,
      .wr_num_handles = handles_size,
      .rd_num_bytes = message_size,
      .rd_num_handles = handles_size,
  };
  uint32_t actual_bytes = 0;
  uint32_t actual_handles = 0;
  ASSERT_EQ(ZX_OK,
            client_end.call_etc(0, zx::time::infinite(), &args, &actual_bytes, &actual_handles));
  ASSERT_EQ(message_size, actual_bytes);
  ASSERT_EQ(handles_size, actual_handles);
  EXPECT_NE(0, response_handles[0].handle);
  EXPECT_EQ(info0.type, response_handles[0].type);
  EXPECT_EQ(info0.rights, response_handles[0].rights);
  EXPECT_NE(0, response_handles[1].handle);
  EXPECT_EQ(info1.type, response_handles[1].type);
  EXPECT_EQ(info1.rights, response_handles[1].rights);
  zx_handle_close(response_handles[0].handle);
  zx_handle_close(response_handles[1].handle);
}

TEST(ChannelCallEtcTest, ReducedRightsSuccessCase) {
  EchoServer echo_server;
  zx::channel client_end = echo_server.ClientEnd();

  constexpr size_t message_size = 4;
  uint8_t request_bytes[message_size];
  uint8_t response_bytes[message_size];

  zx::port port0;
  ASSERT_EQ(ZX_OK, zx::port::create(0, &port0));

  constexpr size_t handles_size = 1;
  zx_handle_disposition_t request_handles[handles_size] = {
      {
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = port0.release(),
          .type = ZX_OBJ_TYPE_PORT,
          .rights = ZX_RIGHT_TRANSFER,
          .result = ZX_OK,
      },
  };
  zx_handle_info_t response_handles[handles_size] = {};

  zx_channel_call_etc_args_t args = {
      .wr_bytes = request_bytes,
      .wr_handles = request_handles,
      .rd_bytes = response_bytes,
      .rd_handles = response_handles,
      .wr_num_bytes = message_size,
      .wr_num_handles = handles_size,
      .rd_num_bytes = message_size,
      .rd_num_handles = handles_size,
  };
  uint32_t actual_bytes = 0;
  uint32_t actual_handles = 0;
  ASSERT_EQ(ZX_OK,
            client_end.call_etc(0, zx::time::infinite(), &args, &actual_bytes, &actual_handles));
  ASSERT_EQ(message_size, actual_bytes);
  ASSERT_EQ(handles_size, actual_handles);
  EXPECT_NE(0, response_handles[0].handle);
  EXPECT_EQ(ZX_OBJ_TYPE_PORT, response_handles[0].type);
  EXPECT_EQ(ZX_RIGHT_TRANSFER, response_handles[0].rights);
  zx_handle_close(response_handles[0].handle);
}

TEST(ChannelCallEtcTest, IncreasedRightsFailureCase) {
  zx::channel client_end;
  zx::channel server_end;
  ASSERT_OK(zx::channel::create(0, &client_end, &server_end));

  constexpr size_t message_size = 4;
  uint8_t request_bytes[message_size];
  uint8_t response_bytes[message_size];

  zx::port port0;
  ASSERT_EQ(ZX_OK, zx::port::create(0, &port0));

  constexpr size_t handles_size = 1;
  zx_handle_disposition_t request_handles[handles_size] = {
      {
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = port0.release(),
          .type = ZX_OBJ_TYPE_PORT,
          .rights = ZX_RIGHT_TRANSFER | ZX_RIGHT_MANAGE_PROCESS,
          .result = ZX_OK,
      },
  };
  zx_handle_info_t response_handles[handles_size] = {};

  zx_channel_call_etc_args_t args = {
      .wr_bytes = request_bytes,
      .wr_handles = request_handles,
      .rd_bytes = response_bytes,
      .rd_handles = response_handles,
      .wr_num_bytes = message_size,
      .wr_num_handles = handles_size,
      .rd_num_bytes = message_size,
      .rd_num_handles = handles_size,
  };
  uint32_t actual_bytes = 0;
  uint32_t actual_handles = 0;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            client_end.call_etc(0, zx::time::infinite(), &args, &actual_bytes, &actual_handles));
  zx_handle_close(request_handles[0].handle);
}

TEST(ChannelCallEtcTest, WrongObjectTypeFailureCase) {
  zx::channel client_end;
  zx::channel server_end;
  ASSERT_OK(zx::channel::create(0, &client_end, &server_end));

  constexpr size_t message_size = 4;
  uint8_t request_bytes[message_size];
  uint8_t response_bytes[message_size];

  zx::port port0;
  ASSERT_EQ(ZX_OK, zx::port::create(0, &port0));

  constexpr size_t handles_size = 1;
  zx_handle_disposition_t request_handles[handles_size] = {
      {
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = port0.release(),
          .type = ZX_OBJ_TYPE_VMO,
          .rights = ZX_RIGHT_SAME_RIGHTS,
          .result = ZX_OK,
      },
  };
  zx_handle_info_t response_handles[handles_size] = {};

  zx_channel_call_etc_args_t args = {
      .wr_bytes = request_bytes,
      .wr_handles = request_handles,
      .rd_bytes = response_bytes,
      .rd_handles = response_handles,
      .wr_num_bytes = message_size,
      .wr_num_handles = handles_size,
      .rd_num_bytes = message_size,
      .rd_num_handles = handles_size,
  };
  uint32_t actual_bytes = 0;
  uint32_t actual_handles = 0;
  ASSERT_EQ(ZX_ERR_WRONG_TYPE,
            client_end.call_etc(0, zx::time::infinite(), &args, &actual_bytes, &actual_handles));
  zx_handle_close(request_handles[0].handle);
}

TEST(ChannelCallEtcTest, BadChannelFailureCase) {
  zx::channel client_end;

  constexpr size_t message_size = 4;
  uint8_t request_bytes[message_size];
  uint8_t response_bytes[message_size];

  zx_channel_call_etc_args_t args = {
      .wr_bytes = request_bytes,
      .wr_handles = nullptr,
      .rd_bytes = response_bytes,
      .rd_handles = nullptr,
      .wr_num_bytes = message_size,
      .wr_num_handles = 0,
      .rd_num_bytes = message_size,
      .rd_num_handles = 0,
  };
  uint32_t actual_bytes = 0;
  uint32_t actual_handles = 0;
  ASSERT_EQ(ZX_ERR_BAD_HANDLE,
            client_end.call_etc(0, zx::time::infinite(), &args, &actual_bytes, &actual_handles));
}

}  // namespace
