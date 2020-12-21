// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/event.h>

#include <cstddef>
#include <cstdint>
#include <thread>

#include <zxtest/zxtest.h>

namespace {

// Wrap an array in a struct so it can be returned.
template <size_t N>
struct iovec_array_result {
  zx_channel_iovec_t value[N];
};

template <size_t N, size_t M>
iovec_array_result<N> iovec_array(const char buffers[N][M]) {
  iovec_array_result<N> result;
  for (size_t i = 0; i < N; i++) {
    result.value[i] = zx_channel_iovec_t{
        .buffer = buffers[i],
        .capacity = M,
        .reserved = 0,
    };
  }
  return result;
}

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
    ASSERT_EQ(ZX_OK, server_end.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));
    ASSERT_EQ(ZX_OK, server_end.read(0, bytes, handles, NBufBytes, NBufHandles, &actual_bytes,
                                     &actual_handles));
    ASSERT_EQ(ZX_OK, server_end.wait_one(ZX_CHANNEL_WRITABLE, zx::time::infinite(), nullptr));
    ASSERT_EQ(ZX_OK, server_end.write(0, bytes, actual_bytes, handles, actual_handles));
  }

  zx::channel client_end;
  std::thread thread;
};

TEST(IOVecTest, WriteZeroIovecs) {
  zx::channel readCh, writeCh;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &readCh, &writeCh));
  ASSERT_EQ(ZX_OK, writeCh.write(ZX_CHANNEL_WRITE_USE_IOVEC, nullptr, 0, nullptr, 0));

  uint32_t actual_bytes, actual_handles;
  ASSERT_EQ(ZX_OK, readCh.read(0, nullptr, nullptr, 0, 0, &actual_bytes, &actual_handles));

  EXPECT_EQ(0, actual_bytes);
  EXPECT_EQ(0, actual_handles);
}

template <size_t NumIovecs, size_t BytesPerIovec>
void write_read_iovecs() {
  zx::channel readCh, writeCh;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &readCh, &writeCh));

  char inputs[NumIovecs][BytesPerIovec];
  for (size_t i = 0; i < NumIovecs; i++) {
    // i/256 is added to the value because iovecs are stored in the kernel at
    // 256 iovecs per page and all pages will look identically otherwise.
    std::fill_n(inputs[i], sizeof(inputs[i]), static_cast<char>(i + (i / 256)));
  }
  auto iovecs = iovec_array<NumIovecs, BytesPerIovec>(inputs);
  ASSERT_EQ(ZX_OK, writeCh.write(ZX_CHANNEL_WRITE_USE_IOVEC, iovecs.value, NumIovecs, nullptr, 0));

  char output[65536];
  uint32_t actual_bytes, actual_handles;
  ASSERT_EQ(ZX_OK,
            readCh.read(0, output, nullptr, sizeof(output), 0, &actual_bytes, &actual_handles));
  ASSERT_EQ(NumIovecs * BytesPerIovec, actual_bytes);
  ASSERT_EQ(0, actual_handles);

  for (size_t i = 0; i < NumIovecs; i++) {
    EXPECT_BYTES_EQ(inputs[i], &output[i * BytesPerIovec], BytesPerIovec);
  }
}

// Test writing a small number of iovecs, which fit within the kernel stack buffer.
TEST(IOVecTest, WriteFewLargeIovecs) { write_read_iovecs<3, 8000>(); }

// Test writing a large number of iovecs, that don't fit within the stack buffer.
TEST(IOVecTest, WriteManySmallIovecs) { write_read_iovecs<1000, 10>(); }

TEST(IOVecTest, WriteWithHandle) {
  zx::channel readCh, writeCh;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &readCh, &writeCh));

  char in_bytes[8] = {};
  zx_channel_iovec_t in_iovecs[] = {
      zx_channel_iovec_t{
          .buffer = in_bytes,
          .capacity = sizeof(in_bytes),
          .reserved = 0,
      },
  };
  zx::event in_handle;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &in_handle));
  ASSERT_EQ(ZX_OK, writeCh.write(ZX_CHANNEL_WRITE_USE_IOVEC, in_iovecs, 1,
                                 reinterpret_cast<zx_handle_t*>(&in_handle), 1));

  char out_bytes[8];
  zx::event out_handles[1] = {};
  uint32_t actual_bytes, actual_handles;
  ASSERT_EQ(ZX_OK,
            readCh.read(0, out_bytes, reinterpret_cast<zx_handle_t*>(out_handles),
                        sizeof(out_bytes), sizeof(out_handles), &actual_bytes, &actual_handles));
  ASSERT_EQ(8, actual_bytes);
  EXPECT_BYTES_EQ(in_bytes, out_bytes, sizeof(in_bytes));
  ASSERT_EQ(1, actual_handles);
  ASSERT_TRUE(out_handles[0].is_valid());
}

TEST(IOVecTest, WriteEtcWithHandle) {
  zx::channel readCh, writeCh;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &readCh, &writeCh));

  char in_bytes[8] = {};
  zx_channel_iovec_t in_iovecs[] = {
      zx_channel_iovec_t{
          .buffer = in_bytes,
          .capacity = sizeof(in_bytes),
          .reserved = 0,
      },
  };
  zx::event event;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &event));
  zx_handle_disposition_t in_handle = {
      .operation = ZX_HANDLE_OP_MOVE,
      .handle = event.get(),
      .type = ZX_OBJ_TYPE_EVENT,
      .rights = ZX_RIGHT_SAME_RIGHTS,
      .result = ZX_OK,
  };
  ASSERT_EQ(ZX_OK, writeCh.write_etc(ZX_CHANNEL_WRITE_USE_IOVEC, in_iovecs, 1, &in_handle, 1));

  char out_bytes[8];
  zx::event out_handles[1] = {};
  uint32_t actual_bytes, actual_handles;
  ASSERT_EQ(ZX_OK,
            readCh.read(0, out_bytes, reinterpret_cast<zx_handle_t*>(out_handles),
                        sizeof(out_bytes), sizeof(out_handles), &actual_bytes, &actual_handles));
  ASSERT_EQ(8, actual_bytes);
  EXPECT_BYTES_EQ(in_bytes, out_bytes, sizeof(in_bytes));
  ASSERT_EQ(1, actual_handles);
  ASSERT_TRUE(out_handles[0].is_valid());
}

template <size_t NumIovecs, size_t BytesPerIovec>
void check_for_out_of_range_write() {
  zx::channel readCh, writeCh;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &readCh, &writeCh));
  char inputs[NumIovecs][BytesPerIovec];
  auto iovecs = iovec_array<NumIovecs, BytesPerIovec>(inputs);
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE,
            writeCh.write(ZX_CHANNEL_WRITE_USE_IOVEC, iovecs.value, NumIovecs, nullptr, 0));
}

TEST(IOVecTest, WriteTooManyIovecs) { check_for_out_of_range_write<9000, 1>(); }
TEST(IOVecTest, WriteTooManyBytes) { check_for_out_of_range_write<1000, 100>(); }

TEST(IOVecTest, WriteNonZeroReservedIovec) {
  zx::channel readCh, writeCh;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &readCh, &writeCh));
  char buffer[256];
  zx_channel_iovec_t iovecs[]{
      zx_channel_iovec_t{
          .buffer = buffer,
          .capacity = sizeof(buffer),
          .reserved = 1,
      },
  };
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, writeCh.write(ZX_CHANNEL_WRITE_USE_IOVEC, iovecs, 1, nullptr, 0));
}

TEST(IOVecTest, WriteNullBufferNonnullSize) {
  zx::channel readCh, writeCh;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &readCh, &writeCh));
  zx_channel_iovec_t iovecs[]{
      zx_channel_iovec_t{
          .buffer = nullptr,
          .capacity = 8,
          .reserved = 0,
      },
  };
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, writeCh.write(ZX_CHANNEL_WRITE_USE_IOVEC, iovecs, 1, nullptr, 0));
}

TEST(IOVecTest, CallIovecBytesLessThanTxidSize) {
  zx::channel clientCh, serverCh;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &clientCh, &serverCh));

  char inputs[3];
  zx_channel_iovec_t iovecs[] = {
      zx_channel_iovec_t{
          .buffer = inputs,
          .capacity = sizeof(inputs),
          .reserved = 0,
      },
  };
  zx_channel_call_args_t args = {
      .wr_bytes = iovecs,
      .wr_handles = nullptr,
      .rd_bytes = nullptr,
      .rd_handles = nullptr,
      .wr_num_bytes = 1,
      .wr_num_handles = 0,
      .rd_num_bytes = 0,
      .rd_num_handles = 0,
  };
  uint32_t actual_bytes, actual_handles;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, clientCh.call(ZX_CHANNEL_WRITE_USE_IOVEC, zx::time::infinite(),
                                               &args, &actual_bytes, &actual_handles));
}

template <size_t NumIovecs, size_t BytesPerIovec>
void call_iovecs() {
  EchoServer echo_server;
  zx::channel clientCh = echo_server.ClientEnd();

  char inputs[NumIovecs][BytesPerIovec];
  for (size_t i = 0; i < NumIovecs; i++) {
    // i/256 is added to the value because iovecs are stored in the kernel at
    // 256 iovecs per page and all pages will look identically otherwise.
    std::fill_n(inputs[i], sizeof(inputs[i]), static_cast<char>(i + (i / 256)));
  }
  auto iovecs = iovec_array<NumIovecs, BytesPerIovec>(inputs);

  char output[NumIovecs * BytesPerIovec];
  zx_channel_call_args_t args{
      .wr_bytes = iovecs.value,
      .wr_handles = nullptr,
      .rd_bytes = output,
      .rd_handles = nullptr,
      .wr_num_bytes = NumIovecs,
      .wr_num_handles = 0,
      .rd_num_bytes = sizeof(output),
      .rd_num_handles = 0,
  };
  uint32_t actual_bytes, actual_handles;
  ASSERT_EQ(ZX_OK, clientCh.call(ZX_CHANNEL_WRITE_USE_IOVEC, zx::time::infinite(), &args,
                                 &actual_bytes, &actual_handles));
  ASSERT_EQ(NumIovecs * BytesPerIovec, actual_bytes);
  ASSERT_EQ(0, actual_handles);

  char expected_output[NumIovecs * BytesPerIovec];
  for (size_t i = 0; i < NumIovecs; i++) {
    memcpy(&expected_output[i * BytesPerIovec], inputs[i], BytesPerIovec);
  }
  EXPECT_BYTES_EQ(expected_output + sizeof(zx_txid_t), output + +sizeof(zx_txid_t),
                  NumIovecs * BytesPerIovec - sizeof(zx_txid_t));
}

// Test writing a small number of iovecs, which fit within the kernel stack buffer.
TEST(IOVecTest, CallFewLargeIovecs) { call_iovecs<3, 8000>(); }

// Test writing a large number of iovecs, that don't fit within the stack buffer.
TEST(IOVecTest, CallManySmallIovecs) { call_iovecs<1000, 10>(); }

TEST(IOVecTest, CallWithHandle) {
  EchoServer echo_server;
  zx::channel clientCh = echo_server.ClientEnd();

  char in_bytes[8] = {};
  zx_channel_iovec_t in_iovecs[] = {
      zx_channel_iovec_t{
          .buffer = in_bytes,
          .capacity = sizeof(in_bytes),
          .reserved = 0,
      },
  };
  zx::event in_handle;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &in_handle));

  char out_bytes[8];
  zx::event out_handles[1] = {};

  zx_channel_call_args_t args{
      .wr_bytes = in_iovecs,
      .wr_handles = reinterpret_cast<zx_handle_t*>(&in_handle),
      .rd_bytes = out_bytes,
      .rd_handles = reinterpret_cast<zx_handle_t*>(out_handles),
      .wr_num_bytes = 1,
      .wr_num_handles = 1,
      .rd_num_bytes = 8,
      .rd_num_handles = 1,
  };
  uint32_t actual_bytes, actual_handles;
  ASSERT_EQ(ZX_OK, clientCh.call(ZX_CHANNEL_WRITE_USE_IOVEC, zx::time::infinite(), &args,
                                 &actual_bytes, &actual_handles));
  ASSERT_EQ(8, actual_bytes);
  EXPECT_BYTES_EQ(in_bytes + sizeof(zx_txid_t), out_bytes + sizeof(zx_txid_t),
                  sizeof(in_bytes) - sizeof(zx_txid_t));
  ASSERT_EQ(1, actual_handles);
  ASSERT_TRUE(out_handles[0].is_valid());
}

TEST(IOVecTest, CallEtcWithHandle) {
  EchoServer echo_server;
  zx::channel clientCh = echo_server.ClientEnd();

  char in_bytes[8] = {};
  zx_channel_iovec_t in_iovecs[] = {
      zx_channel_iovec_t{
          .buffer = in_bytes,
          .capacity = sizeof(in_bytes),
          .reserved = 0,
      },
  };
  zx::event in_handle;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &in_handle));
  zx_handle_disposition_t in_hds[] = {
      zx_handle_disposition_t{
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = in_handle.get(),
          .type = ZX_OBJ_TYPE_EVENT,
          .rights = ZX_RIGHT_SAME_RIGHTS,
          .result = ZX_OK,
      },
  };

  char out_bytes[8];
  zx_handle_info_t out_his[1];

  zx_channel_call_etc_args_t args{
      .wr_bytes = in_iovecs,
      .wr_handles = in_hds,
      .rd_bytes = out_bytes,
      .rd_handles = out_his,
      .wr_num_bytes = 1,
      .wr_num_handles = 1,
      .rd_num_bytes = 8,
      .rd_num_handles = 1,
  };
  uint32_t actual_bytes, actual_handles;
  ASSERT_EQ(ZX_OK, clientCh.call_etc(ZX_CHANNEL_WRITE_USE_IOVEC, zx::time::infinite(), &args,
                                     &actual_bytes, &actual_handles));
  ASSERT_EQ(8, actual_bytes);
  EXPECT_BYTES_EQ(in_bytes + sizeof(zx_txid_t), out_bytes + sizeof(zx_txid_t),
                  sizeof(in_bytes) - sizeof(zx_txid_t));
  ASSERT_EQ(1, actual_handles);
  ASSERT_NE(ZX_HANDLE_INVALID, out_his[0].handle);
  ASSERT_EQ(ZX_OBJ_TYPE_EVENT, out_his[0].type);

  zx_handle_close(out_his[0].handle);
}

template <size_t NumIovecs, size_t BytesPerIovec>
void check_for_out_of_range_call() {
  zx::channel clientCh, serverCh;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &clientCh, &serverCh));
  char inputs[NumIovecs][BytesPerIovec];
  auto iovecs = iovec_array<NumIovecs, BytesPerIovec>(inputs);
  zx_channel_call_etc_args_t args{
      .wr_bytes = iovecs.value,
      .wr_handles = nullptr,
      .rd_bytes = nullptr,
      .rd_handles = nullptr,
      .wr_num_bytes = NumIovecs,
      .wr_num_handles = 0,
      .rd_num_bytes = 0,
      .rd_num_handles = 0,
  };
  uint32_t actual_bytes, actual_handles;
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, clientCh.call_etc(ZX_CHANNEL_WRITE_USE_IOVEC, zx::time::infinite(),
                                                   &args, &actual_bytes, &actual_handles));
}

TEST(IOVecTest, CallTooManyIovecs) { check_for_out_of_range_call<9000, 1>(); }
TEST(IOVecTest, CallTooManyBytes) { check_for_out_of_range_call<1000, 100>(); }

TEST(IOVecTest, CallNonZeroReservedIovec) {
  zx::channel clientCh, serverCh;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &clientCh, &serverCh));
  char buffer[256];
  zx_channel_iovec_t iovecs[]{
      zx_channel_iovec_t{
          .buffer = buffer,
          .capacity = sizeof(buffer),
          .reserved = 1,
      },
  };
  zx_channel_call_args_t args{
      .wr_bytes = iovecs,
      .wr_handles = nullptr,
      .rd_bytes = nullptr,
      .rd_handles = nullptr,
      .wr_num_bytes = 1,
      .wr_num_handles = 0,
      .rd_num_bytes = 0,
      .rd_num_handles = 0,
  };
  uint32_t actual_bytes, actual_handles;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, clientCh.call(ZX_CHANNEL_WRITE_USE_IOVEC, zx::time::infinite(),
                                               &args, &actual_bytes, &actual_handles));
}

TEST(IOVecTest, CallNullBufferNonnullSize) {
  zx::channel clientCh, serverCh;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &clientCh, &serverCh));
  zx_channel_iovec_t iovecs[]{
      zx_channel_iovec_t{
          .buffer = nullptr,
          .capacity = 8,
          .reserved = 0,
      },
  };
  zx_channel_call_args_t args{
      .wr_bytes = iovecs,
      .wr_handles = nullptr,
      .rd_bytes = nullptr,
      .rd_handles = nullptr,
      .wr_num_bytes = 1,
      .wr_num_handles = 0,
      .rd_num_bytes = 0,
      .rd_num_handles = 0,
  };
  uint32_t actual_bytes, actual_handles;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, clientCh.call(ZX_CHANNEL_WRITE_USE_IOVEC, zx::time::infinite(),
                                               &args, &actual_bytes, &actual_handles));
}

}  // namespace
