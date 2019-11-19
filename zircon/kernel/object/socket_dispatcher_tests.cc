// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <lib/unittest/user_memory.h>
#include <lib/user_copy/user_ptr.h>

#include <object/socket_dispatcher.h>

namespace {

// Allocate/destroy many sockets. Ad hoc resource leak check.
bool TestCreateDestroyManySockets() {
  BEGIN_TEST;
  static constexpr int kMany = 750000;

  for (uint32_t flags : {ZX_SOCKET_STREAM, ZX_SOCKET_DATAGRAM}) {
    for (int i = 0; i < kMany; i++) {
      KernelHandle<SocketDispatcher> dispatcher0, dispatcher1;
      zx_rights_t rights;
      auto status = SocketDispatcher::Create(/*flags=*/flags, &dispatcher0, &dispatcher1, &rights);
      ASSERT_EQ(status, ZX_OK);
    }
  }

  END_TEST;
}

// Stream socket write/read test.
bool TestCreateWriteReadClose() {
  BEGIN_TEST;

  static constexpr unsigned int kSize = 3357;
  ktl::unique_ptr<testing::UserMemory> write = testing::UserMemory::Create(kSize);
  ktl::unique_ptr<testing::UserMemory> read = testing::UserMemory::Create(1);

  KernelHandle<SocketDispatcher> dispatcher0, dispatcher1;
  zx_rights_t rights;
  auto status =
      SocketDispatcher::Create(/*flags=*/ZX_SOCKET_STREAM, &dispatcher0, &dispatcher1, &rights);
  ASSERT_EQ(status, ZX_OK);

  zx_info_socket info = {};
  dispatcher0.dispatcher()->GetInfo(&info);
  ASSERT_EQ(info.rx_buf_available, 0u);  // No bytes written yet.

  // Write a test pattern, read it back one byte at a time.
  auto* const write_buffer = reinterpret_cast<unsigned char*>(write->out<char>());
  for (uint i = 0; i < kSize; i++) {
    write_buffer[i] = static_cast<unsigned char>(i);
  }
  size_t written = 0;
  auto write_status =
      dispatcher0.dispatcher()->Write(make_user_in_ptr(write->in<char>()), kSize, &written);
  EXPECT_EQ(write_status, ZX_OK);
  EXPECT_EQ(written, kSize);
  // Expect to not be able to read on the dispatcher side you just wrote to
  dispatcher0.dispatcher()->GetInfo(&info);
  EXPECT_EQ(info.rx_buf_available, 0u);
  // Expect to be able to read from the paired dispatcher.
  dispatcher1.dispatcher()->GetInfo(&info);
  EXPECT_EQ(info.rx_buf_available, kSize);

  // Read out data from the peer byte-at-a-time; this is a stream socket, allowing that.
  unsigned char read_buffer[kSize] = {};
  for (uint i = 0; i < kSize; i++) {
    size_t bytes_read = 0;
    auto read_status = dispatcher1.dispatcher()->Read(
        SocketDispatcher::ReadType::kConsume, make_user_out_ptr(read->out<char>()), 1, &bytes_read);
    EXPECT_EQ(read_status, ZX_OK);
    EXPECT_EQ(bytes_read, 1u);
    // Expect consuming 1-byte reads to reduce rx_buf_available.
    dispatcher1.dispatcher()->GetInfo(&info);
    EXPECT_EQ(info.rx_buf_available, kSize - (i + 1));
    read_buffer[i] = *reinterpret_cast<const unsigned char*>(read->in<char>());
  }
  EXPECT_EQ(memcmp(read_buffer, write_buffer, kSize), 0);

  // Test that shutting down a socket for writes still allows reads from the paired dispatcher
  EXPECT_EQ(dispatcher0.dispatcher()->Write(make_user_in_ptr(write->in<char>()), 1, &written), ZX_OK);
  EXPECT_EQ(written, 1u);
  EXPECT_EQ(dispatcher0.dispatcher()->Shutdown(ZX_SOCKET_SHUTDOWN_WRITE), ZX_OK);
  EXPECT_EQ(dispatcher0.dispatcher()->Write(make_user_in_ptr(write->in<char>()), 1, &written),
            ZX_ERR_BAD_STATE);  // |written| is not updated if Write() fails.
  dispatcher1.dispatcher()->GetInfo(&info);
  EXPECT_EQ(info.rx_buf_available, 1u);  // Not 2 - the second write must have failed.

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(socket_dispatcher_tests)
UNITTEST("TestCreateDestroyManySockets", TestCreateDestroyManySockets)
UNITTEST("TestCreateWriteReadClose", TestCreateWriteReadClose)
UNITTEST_END_TESTCASE(socket_dispatcher_tests, "socket_dispatcher_tests", "SocketDispatcher tests")
