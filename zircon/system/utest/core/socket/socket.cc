// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/array.h>
#include <lib/zx/socket.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <utility>
#include <zxtest/zxtest.h>

namespace {

constexpr char kMsg1[] = "12345";
constexpr uint32_t kMsg1Len = fbl::constexpr_strlen(kMsg1);
constexpr char kMsg2[] = "abcde";
constexpr uint32_t kMsg2Len = fbl::constexpr_strlen(kMsg2);
constexpr char kMsg3[] = "fghij";
constexpr uint32_t kMsg3Len = fbl::constexpr_strlen(kMsg3);

zx_signals_t GetSignals(const zx::socket& socket) {
  zx_signals_t pending = 0;
  socket.wait_one(0u, zx::time(), &pending);
  return pending;
}

TEST(SocketTest, EndpointsAreRelated) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  // Check that koids line up.
  zx_info_handle_basic_t info_local = {}, info_remote = {};
  ASSERT_OK(
      local.get_info(ZX_INFO_HANDLE_BASIC, &info_local, sizeof(info_local), nullptr, nullptr));
  ASSERT_OK(
      remote.get_info(ZX_INFO_HANDLE_BASIC, &info_remote, sizeof(info_remote), nullptr, nullptr));
  EXPECT_NE(info_local.koid, 0u, "zero koid!");
  EXPECT_NE(info_local.related_koid, 0u, "zero peer koid!");
  EXPECT_NE(info_remote.koid, 0u, "zero koid!");
  EXPECT_NE(info_remote.related_koid, 0u, "zero peer koid!");
  EXPECT_EQ(info_local.koid, info_remote.related_koid, "mismatched koids!");
  EXPECT_EQ(info_remote.koid, info_local.related_koid, "mismatched koids!");
}

TEST(SocketTest, EmptySocketShouldWait) {
  zx::socket local, remote;
  uint32_t read_data[] = {0, 0};
  size_t count;

  ASSERT_OK(zx::socket::create(0, &local, &remote));
  EXPECT_EQ(local.read(0u, read_data, sizeof(read_data), &count), ZX_ERR_SHOULD_WAIT);
}

TEST(SocketTest, WriteReadDataVerify) {
  zx::socket local, remote;
  uint32_t read_data[] = {0, 0};
  size_t count;

  ASSERT_OK(zx::socket::create(0, &local, &remote));
  constexpr uint32_t write_data[] = {0xdeadbeef, 0xc0ffee};
  EXPECT_OK(local.write(0u, &write_data[0], sizeof(write_data[0]), &count));
  EXPECT_EQ(count, sizeof(write_data[0]));
  EXPECT_OK(local.write(0u, &write_data[1], sizeof(write_data[1]), &count));
  EXPECT_EQ(count, sizeof(write_data[1]));

  EXPECT_OK(remote.read(0u, read_data, sizeof(read_data), &count));
  EXPECT_EQ(count, sizeof(read_data));
  EXPECT_EQ(read_data[0], write_data[0]);
  EXPECT_EQ(read_data[1], write_data[1]);

  EXPECT_OK(local.write(0u, write_data, sizeof(write_data), nullptr));
  memset(read_data, 0, sizeof(read_data));
  EXPECT_OK(remote.read(0u, read_data, sizeof(read_data), nullptr));
  EXPECT_EQ(read_data[0], write_data[0]);
  EXPECT_EQ(read_data[1], write_data[1]);
}

TEST(SocketTest, PeerClosedError) {
  zx::socket local;
  size_t count;
  {
    zx::socket remote;
    ASSERT_OK(zx::socket::create(0, &local, &remote));
    // remote gets closed here.
  }

  constexpr uint32_t write_data[] = {0xdeadbeef, 0xc0ffee};
  EXPECT_EQ(local.write(0u, &write_data[1], sizeof(write_data[1]), &count), ZX_ERR_PEER_CLOSED);
}

TEST(SocketTest, PeekingLeavesData) {
  size_t count;
  zx::socket local, remote;
  uint32_t read_data[] = {0, 0};
  constexpr uint32_t write_data[] = {0xdeadbeef, 0xc0ffee};

  ASSERT_OK(zx::socket::create(0, &local, &remote));

  EXPECT_OK(local.write(0u, &write_data[0], sizeof(write_data[0]), &count));
  EXPECT_EQ(count, sizeof(write_data[0]));
  EXPECT_OK(local.write(0u, &write_data[1], sizeof(write_data[1]), &count));
  EXPECT_EQ(count, sizeof(write_data[1]));

  EXPECT_OK(remote.read(ZX_SOCKET_PEEK, read_data, sizeof(read_data), &count));
  EXPECT_EQ(count, sizeof(read_data));
  EXPECT_EQ(read_data[0], write_data[0]);
  EXPECT_EQ(read_data[1], write_data[1]);

  // The message should still be pending for h1 to read.
  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE);

  memset(read_data, 0, sizeof(read_data));
  EXPECT_OK(remote.read(0u, read_data, sizeof(read_data), &count));
  EXPECT_EQ(count, sizeof(read_data));
  EXPECT_EQ(read_data[0], write_data[0]);
  EXPECT_EQ(read_data[1], write_data[1]);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);
}

TEST(SocketTest, PeekingIntoEmpty) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));
  size_t count;
  char data;
  EXPECT_EQ(local.read(ZX_SOCKET_PEEK, &data, sizeof(data), &count), ZX_ERR_SHOULD_WAIT);
}

TEST(SocketTest, Signals) {
  size_t count;
  zx::socket local;

  {
    zx::socket remote;
    ASSERT_OK(zx::socket::create(0, &local, &remote));

    EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
    EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);

    const size_t kAllSize = 128 * 1024;
    fbl::Array<char> big_buf(new char[kAllSize], kAllSize);
    ASSERT_NOT_NULL(big_buf.data());

    memset(big_buf.data(), 0x66, kAllSize);

    EXPECT_OK(local.write(0u, big_buf.data(), kAllSize / 16, &count));
    EXPECT_EQ(count, kAllSize / 16);

    EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
    EXPECT_EQ(GetSignals(remote), ZX_SOCKET_READABLE | ZX_SOCKET_WRITABLE);

    EXPECT_OK(remote.read(0u, big_buf.data(), kAllSize, &count));
    EXPECT_EQ(count, kAllSize / 16);

    EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
    EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);

    EXPECT_EQ(local.signal_peer(ZX_SOCKET_WRITABLE, 0u), ZX_ERR_INVALID_ARGS);

    EXPECT_OK(local.signal_peer(0u, ZX_USER_SIGNAL_1));

    EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
    EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE | ZX_USER_SIGNAL_1);
    // remote closed
  }

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_PEER_CLOSED);
}

TEST(SocketTest, SetThreshholdsProp) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));
  size_t count;

  /* Set some valid and invalid threshold values and verify */
  count = 0;
  EXPECT_OK(local.set_property(ZX_PROP_SOCKET_RX_THRESHOLD, &count, sizeof(size_t)));
  count = 0xefffffff;
  EXPECT_EQ(local.set_property(ZX_PROP_SOCKET_RX_THRESHOLD, &count, sizeof(size_t)),
            ZX_ERR_INVALID_ARGS);
  count = 0;
  EXPECT_OK(local.set_property(ZX_PROP_SOCKET_TX_THRESHOLD, &count, sizeof(size_t)));
  count = 0xefffffff;
  EXPECT_EQ(local.set_property(ZX_PROP_SOCKET_TX_THRESHOLD, &count, sizeof(size_t)),
            ZX_ERR_INVALID_ARGS);
}

TEST(SocketTest, SetThreshholdsAndCheckSignals) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));
  size_t count;

  /*
   * In the code below, we are going to trigger the READ threshold
   * signal as soon as 101 bytes are available to read, and trigger
   * the WRITE threshold as long as we have 103 bytes we can write/
   */

  /* Set valid Read/Write thresholds and verify */
  constexpr size_t SOCKET2_SIGNALTEST_RX_THRESHOLD = 101;
  count = SOCKET2_SIGNALTEST_RX_THRESHOLD;
  EXPECT_OK(local.set_property(ZX_PROP_SOCKET_RX_THRESHOLD, &count, sizeof(size_t)));

  EXPECT_OK(local.get_property(ZX_PROP_SOCKET_RX_THRESHOLD, &count, sizeof(size_t)));
  ASSERT_EQ(count, (size_t)SOCKET2_SIGNALTEST_RX_THRESHOLD);

  zx_info_socket_t info;
  memset(&info, 0, sizeof(info));
  ASSERT_OK(remote.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  size_t write_threshold = info.tx_buf_max - (SOCKET2_SIGNALTEST_RX_THRESHOLD + 2);
  ASSERT_OK(remote.set_property(ZX_PROP_SOCKET_TX_THRESHOLD, &write_threshold, sizeof(size_t)));
  ASSERT_OK(remote.get_property(ZX_PROP_SOCKET_TX_THRESHOLD, &count, sizeof(size_t)));
  ASSERT_EQ(count, write_threshold);

  /* Make sure duplicates get the same thresholds ! */
  zx::socket local_clone, remote_clone;
  ASSERT_OK(local.duplicate(ZX_RIGHT_SAME_RIGHTS, &local_clone));
  ASSERT_OK(remote.duplicate(ZX_RIGHT_SAME_RIGHTS, &remote_clone));

  ASSERT_OK(local_clone.get_property(ZX_PROP_SOCKET_RX_THRESHOLD, &count, sizeof(size_t)));
  ASSERT_EQ(count, (size_t)SOCKET2_SIGNALTEST_RX_THRESHOLD);
  ASSERT_OK(remote_clone.get_property(ZX_PROP_SOCKET_TX_THRESHOLD, &count, sizeof(size_t)));
  ASSERT_EQ(count, write_threshold);

  /* Test starting signal state after setting thresholds */
  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(local_clone), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);
  EXPECT_EQ(GetSignals(remote_clone), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);

  /* Write data and test signals */
  size_t bufsize = SOCKET2_SIGNALTEST_RX_THRESHOLD - 1;
  char buf[bufsize];
  EXPECT_OK(remote.write(0u, buf, bufsize, &count));
  EXPECT_EQ(count, bufsize);

  /*
   * We wrote less than the read and write thresholds. So we expect
   * the READ_THRESHOLD signal to be de-asserted and the WRITE_THRESHOLD
   * signal to be asserted.
   */
  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE);
  EXPECT_EQ(GetSignals(local_clone), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE);
  EXPECT_EQ(GetSignals(remote_clone), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);

  /*
   * Now write exactly enough data to hit the read threshold
   */
  bufsize = 1;
  EXPECT_OK(remote.write(0u, buf, bufsize, &count));
  EXPECT_EQ(count, bufsize);
  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_READ_THRESHOLD);
  EXPECT_EQ(GetSignals(local_clone),
            ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_READ_THRESHOLD);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);
  EXPECT_EQ(GetSignals(remote_clone), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);

  /*
   * Bump up the read threshold and make sure the READ THRESHOLD signal gets
   * deasserted (and then restore the read threshold back).
   */
  count = SOCKET2_SIGNALTEST_RX_THRESHOLD + 50;
  ASSERT_OK(local.set_property(ZX_PROP_SOCKET_RX_THRESHOLD, &count, sizeof(size_t)));
  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE);
  EXPECT_EQ(GetSignals(local_clone), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE);
  count = SOCKET2_SIGNALTEST_RX_THRESHOLD;
  EXPECT_OK(local.set_property(ZX_PROP_SOCKET_RX_THRESHOLD, &count, sizeof(size_t)));
  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_READ_THRESHOLD);
  EXPECT_EQ(GetSignals(local_clone),
            ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_READ_THRESHOLD);

  /*
   * Bump the write threshold way up and make sure the WRITE THRESHOLD signal gets
   * deasserted (and then restore the write threshold back).
   */
  count = info.tx_buf_max - 10;
  ASSERT_OK(remote.set_property(ZX_PROP_SOCKET_TX_THRESHOLD, &count, sizeof(size_t)));
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(remote_clone), ZX_SOCKET_WRITABLE);
  count = write_threshold;
  EXPECT_OK(remote.set_property(ZX_PROP_SOCKET_TX_THRESHOLD, &count, sizeof(size_t)));
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);
  EXPECT_EQ(GetSignals(remote_clone), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);
  /*
   * Next write enough data to de-assert WRITE Threshold
   */
  bufsize = write_threshold - (SOCKET2_SIGNALTEST_RX_THRESHOLD + 1);
  fbl::Array<char> buf2(new char[bufsize], bufsize);
  EXPECT_OK(remote.write(0u, buf2.data(), bufsize, &count));
  EXPECT_EQ(count, bufsize);
  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_READ_THRESHOLD);
  EXPECT_EQ(GetSignals(local_clone),
            ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_READ_THRESHOLD);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(remote_clone), ZX_SOCKET_WRITABLE);

  /*
   * Finally read enough data to de-assert the read threshold and
   * re-assert the write threshold signals.
   */
  bufsize += 10;
  buf2.reset(new char[bufsize], bufsize);
  EXPECT_OK(local.read(0u, buf2.data(), bufsize, &count));
  EXPECT_EQ(count, bufsize);
  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE);
  EXPECT_EQ(GetSignals(local_clone), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);
  EXPECT_EQ(GetSignals(remote_clone), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);
}

TEST(SocketTest, SignalClosedPeer) {
  zx::socket local;
  {
    zx::socket remote;
    ASSERT_OK(zx::socket::create(0, &local, &remote));
    // remote closed
  }
  ASSERT_EQ(local.signal_peer(0u, ZX_USER_SIGNAL_0), ZX_ERR_PEER_CLOSED);
}

TEST(SocketTest, PeerClosedSetProperty) {
  zx::socket local;
  size_t t = 1;
  {
    zx::socket remote;
    ASSERT_OK(zx::socket::create(0, &local, &remote));

    ASSERT_EQ(local.set_property(ZX_PROP_SOCKET_TX_THRESHOLD, &t, sizeof(t)), ZX_OK);
    // remote closed
  }
  ASSERT_EQ(local.set_property(ZX_PROP_SOCKET_TX_THRESHOLD, &t, sizeof(t)), ZX_ERR_PEER_CLOSED);
}

TEST(SocketTest, ShutdownWrite) {
  size_t count;
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);

  EXPECT_OK(remote.write(0u, kMsg1, kMsg1Len, &count));
  EXPECT_EQ(count, 5);

  EXPECT_OK(remote.shutdown(ZX_SOCKET_SHUTDOWN_WRITE));

  EXPECT_EQ(GetSignals(local),
            ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITE_DISABLED);

  EXPECT_OK(local.write(0u, kMsg2, kMsg2Len, &count));
  EXPECT_EQ(count, 5);

  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_READABLE | ZX_SOCKET_WRITE_DISABLED);

  EXPECT_EQ(remote.write(0u, kMsg3, kMsg3Len, &count), ZX_ERR_BAD_STATE);

  char rbuf[10] = {0};

  EXPECT_OK(local.read(0u, rbuf, sizeof(rbuf), &count));
  EXPECT_EQ(count, 5);
  EXPECT_BYTES_EQ(rbuf, kMsg1, kMsg1Len);

  EXPECT_EQ(local.read(0u, rbuf, 1u, &count), ZX_ERR_BAD_STATE);

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_WRITE_DISABLED);

  EXPECT_OK(remote.read(0u, rbuf, sizeof(rbuf), &count));
  EXPECT_EQ(count, 5);
  EXPECT_BYTES_EQ(rbuf, kMsg2, kMsg2Len);

  local.reset();

  // Calling shutdown after the peer is closed is completely valid.
  EXPECT_OK(remote.shutdown(ZX_SOCKET_SHUTDOWN_READ));

  EXPECT_EQ(GetSignals(remote),
            ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED);

  remote.reset();
}

TEST(SocketTest, ShutdownRead) {
  size_t count;
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);

  EXPECT_OK(remote.write(0u, kMsg1, kMsg1Len, &count));
  EXPECT_EQ(count, 5);

  EXPECT_OK(local.shutdown(ZX_SOCKET_SHUTDOWN_READ));

  EXPECT_EQ(GetSignals(local),
            ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITE_DISABLED);

  EXPECT_OK(local.write(0u, kMsg2, kMsg2Len, &count));
  EXPECT_EQ(count, 5);

  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_READABLE | ZX_SOCKET_WRITE_DISABLED);

  EXPECT_EQ(remote.write(0u, kMsg3, kMsg3Len, &count), ZX_ERR_BAD_STATE);

  char rbuf[10] = {0};

  EXPECT_OK(local.read(0u, rbuf, sizeof(rbuf), &count));
  EXPECT_EQ(count, 5);
  EXPECT_BYTES_EQ(rbuf, kMsg1, kMsg1Len);

  EXPECT_EQ(local.read(0u, rbuf, 1u, &count), ZX_ERR_BAD_STATE);
  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_WRITE_DISABLED);

  EXPECT_OK(remote.read(0u, rbuf, sizeof(rbuf), &count));
  EXPECT_EQ(count, 5);
  EXPECT_BYTES_EQ(rbuf, kMsg2, kMsg2Len);
}

TEST(SocketTest, BytesOutstanding) {
  size_t count;
  zx::socket local;
  constexpr uint32_t write_data[] = {0xdeadbeef, 0xc0ffee};

  {
    zx::socket remote;
    ASSERT_OK(zx::socket::create(0, &local, &remote));
    uint32_t read_data[] = {0, 0};

    EXPECT_EQ(local.read(0u, read_data, sizeof(read_data), &count), ZX_ERR_SHOULD_WAIT);

    EXPECT_OK(local.write(0u, &write_data[0], sizeof(write_data[0]), &count));
    EXPECT_EQ(count, sizeof(write_data[0]));
    EXPECT_OK(local.write(0u, &write_data[1], sizeof(write_data[1]), &count));
    EXPECT_EQ(count, sizeof(write_data[1]));

    // Check the number of bytes outstanding.
    zx_info_socket_t info;
    memset(&info, 0, sizeof(info));
    EXPECT_OK(remote.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
    EXPECT_EQ(info.rx_buf_available, sizeof(write_data));

    // Check that the prior zx_socket_read call didn't disturb the pending data.
    EXPECT_OK(remote.read(0u, read_data, sizeof(read_data), &count));
    EXPECT_EQ(count, sizeof(read_data));
    EXPECT_EQ(read_data[0], write_data[0]);
    EXPECT_EQ(read_data[1], write_data[1]);

    // remote is closed
  }

  EXPECT_EQ(local.write(0u, &write_data[1], sizeof(write_data[1]), &count), ZX_ERR_PEER_CLOSED);
}

TEST(SocketTest, ShutdownWriteBytesOutstanding) {
  size_t count;
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
  EXPECT_OK(remote.write(0u, kMsg1, kMsg1Len, &count));
  EXPECT_EQ(count, 5);

  EXPECT_OK(remote.shutdown(ZX_SOCKET_SHUTDOWN_WRITE));

  EXPECT_EQ(GetSignals(local),
            ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITE_DISABLED);

  EXPECT_OK(local.write(0u, kMsg2, kMsg2Len, &count));
  EXPECT_EQ(count, 5);

  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_READABLE | ZX_SOCKET_WRITE_DISABLED);

  EXPECT_EQ(remote.write(0u, kMsg3, kMsg3Len, &count), ZX_ERR_BAD_STATE);

  char rbuf[10] = {0};

  zx_info_socket_t info;
  memset(&info, 0, sizeof(info));
  EXPECT_OK(local.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.rx_buf_available, 5);
  count = 0;

  EXPECT_OK(local.read(0u, rbuf, sizeof(rbuf), &count));
  EXPECT_EQ(count, 5);
  EXPECT_BYTES_EQ(rbuf, kMsg1, kMsg1Len);

  EXPECT_EQ(local.read(0u, rbuf, 1u, &count), ZX_ERR_BAD_STATE);

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_WRITE_DISABLED);

  EXPECT_OK(remote.read(0u, rbuf, sizeof(rbuf), &count));
  EXPECT_EQ(count, 5);
  EXPECT_BYTES_EQ(rbuf, kMsg2, kMsg2Len);
}

TEST(SocketTest, ShutdownReadBytesOutstanding) {
  size_t count;
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);

  EXPECT_OK(remote.write(0u, kMsg1, kMsg1Len, &count));
  EXPECT_EQ(count, 5);

  EXPECT_OK(local.shutdown(ZX_SOCKET_SHUTDOWN_READ));

  EXPECT_EQ(GetSignals(local),
            ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITE_DISABLED);

  EXPECT_OK(local.write(0u, kMsg2, kMsg2Len, &count));
  EXPECT_EQ(count, 5);

  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_READABLE | ZX_SOCKET_WRITE_DISABLED);

  EXPECT_EQ(remote.write(0u, kMsg3, kMsg3Len, &count), ZX_ERR_BAD_STATE);

  char rbuf[10] = {0};

  zx_info_socket_t info;
  memset(&info, 0, sizeof(info));
  EXPECT_OK(local.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.rx_buf_available, 5);
  count = 0;

  EXPECT_OK(local.read(0u, rbuf, sizeof(rbuf), &count));
  EXPECT_EQ(count, 5);
  EXPECT_BYTES_EQ(rbuf, kMsg1, kMsg1Len);

  EXPECT_EQ(local.read(0u, rbuf, 1u, &count), ZX_ERR_BAD_STATE);

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_WRITE_DISABLED);

  EXPECT_OK(remote.read(0u, rbuf, sizeof(rbuf), &count));
  EXPECT_EQ(count, 5);
  EXPECT_BYTES_EQ(rbuf, kMsg2, kMsg2Len);
}

TEST(SocketTest, ShortWrite) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  // TODO(qsr): Request socket buffer and use (socket_buffer + 1).
  const size_t buffer_size = 256 * 1024 + 1;
  fbl::Array<char> buffer(new char[buffer_size], buffer_size);
  size_t written = ~(size_t)0;  // This should get overwritten by the syscall.
  EXPECT_OK(local.write(0u, buffer.data(), buffer_size, &written));
  EXPECT_LT(written, buffer_size);
}

TEST(SocketTest, Datagram) {
  size_t count;
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  unsigned char rbuf[4096] = {0};  // bigger than an mbuf

  EXPECT_OK(local.write(0u, "packet1", 8u, &count));
  EXPECT_EQ(count, 8);

  EXPECT_OK(local.write(0u, "pkt2", 5u, &count));
  EXPECT_EQ(count, 5);

  rbuf[0] = 'a';
  rbuf[1000] = 'b';
  rbuf[2000] = 'c';
  rbuf[3000] = 'd';
  rbuf[4000] = 'e';
  rbuf[4095] = 'f';
  EXPECT_OK(local.write(0u, rbuf, sizeof(rbuf), &count));
  EXPECT_EQ(count, sizeof(rbuf));

  zx_info_socket_t info;
  memset(&info, 0, sizeof(info));
  EXPECT_OK(remote.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.rx_buf_available, 8);
  count = 0;

  bzero(rbuf, sizeof(rbuf));
  EXPECT_OK(remote.read(0u, rbuf, 3, &count));
  EXPECT_EQ(count, 3);
  EXPECT_BYTES_EQ(rbuf, "pac", 4);  // short read "packet1"
  count = 0;

  memset(&info, 0, sizeof(info));
  EXPECT_OK(remote.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.rx_buf_available, 5);
  count = 0;

  EXPECT_OK(remote.read(0u, rbuf, sizeof(rbuf), &count));
  EXPECT_EQ(count, 5);
  EXPECT_BYTES_EQ(rbuf, "pkt2", 5);

  EXPECT_OK(remote.read(0u, rbuf, sizeof(rbuf), &count));
  EXPECT_EQ(count, sizeof(rbuf));
  EXPECT_EQ(rbuf[0], 'a');
  EXPECT_EQ(rbuf[1000], 'b');
  EXPECT_EQ(rbuf[2000], 'c');
  EXPECT_EQ(rbuf[3000], 'd');
  EXPECT_EQ(rbuf[4000], 'e');
  EXPECT_EQ(rbuf[4095], 'f');

  memset(&info, 0, sizeof(info));
  EXPECT_OK(remote.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.rx_buf_available, 0);
}

TEST(SocketTest, DatagramPeek) {
  size_t count;
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));

  EXPECT_OK(local.write(0u, "pkt1", 5u, &count));
  EXPECT_OK(local.write(0u, "pkt2", 5u, &count));

  char buffer[16];

  // Short peek.
  EXPECT_OK(remote.read(ZX_SOCKET_PEEK, buffer, 3, &count));
  EXPECT_EQ(count, 3);
  EXPECT_BYTES_EQ(buffer, "pkt", 3);

  // Full peek should still see the 1st packet.
  EXPECT_OK(remote.read(ZX_SOCKET_PEEK, buffer, 5, &count));
  EXPECT_EQ(count, 5);
  EXPECT_BYTES_EQ(buffer, "pkt1", 5);

  // Read and consume the 1st packet.
  EXPECT_OK(remote.read(0u, buffer, 5, &count));

  // Now peek should see the 2nd packet.
  EXPECT_OK(remote.read(ZX_SOCKET_PEEK, buffer, 5, &count));
  EXPECT_EQ(count, 5);
  EXPECT_BYTES_EQ(buffer, "pkt2", 5);
}

TEST(SocketTest, DatagramPeekEmpty) {
  size_t count;
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  char data;
  EXPECT_EQ(local.read(ZX_SOCKET_PEEK, &data, sizeof(data), &count), ZX_ERR_SHOULD_WAIT);
}

TEST(SocketTest, DatagramNoShortWrite) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));

  zx_info_socket_t info;
  memset(&info, 0, sizeof(info));
  EXPECT_OK(remote.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  EXPECT_GT(info.tx_buf_max, 0);

  // Pick a size for a huge datagram, and make sure not to overflow.
  size_t buffer_size = info.tx_buf_max * 2;
  EXPECT_GT(buffer_size, 0);

  fbl::Array<char> buffer(new char[buffer_size]{}, buffer_size);
  EXPECT_NOT_NULL(buffer.data());

  size_t written = ~0u;
  EXPECT_EQ(local.write(0u, buffer.data(), buffer_size, &written), ZX_ERR_OUT_OF_RANGE);
  // Since the syscall failed, it should not have overwritten this output
  // parameter.
  EXPECT_EQ(written, ~0u);
}

TEST(SocketTest, ZeroSize) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));
  char buffer;

  EXPECT_EQ(local.read(0, &buffer, 0, nullptr), ZX_ERR_SHOULD_WAIT);
  EXPECT_OK(local.write(0, "a", 1, nullptr));
  EXPECT_OK(remote.read(0, &buffer, 0, nullptr));
  EXPECT_OK(remote.read(0, &buffer, 0, nullptr));

  local.reset();
  remote.reset();
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));

  EXPECT_EQ(local.read(0, &buffer, 0, nullptr), ZX_ERR_SHOULD_WAIT);
  EXPECT_OK(remote.write(0, "a", 1, nullptr));
  EXPECT_OK(local.read(0, &buffer, 0, nullptr));
  EXPECT_OK(local.read(0, &buffer, 0, nullptr));
}

TEST(SocketTest, ReadIntoNullBuffer) {
  zx::socket a, b;
  ASSERT_OK(zx::socket::create(0, &a, &b));

  ASSERT_OK(a.write(0, "A", 1, nullptr));

  size_t actual;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, b.read(0, nullptr, 1, &actual));
}

TEST(SocketTest, ReadIntoBadBuffer) {
  zx::socket a, b;
  ASSERT_OK(zx::socket::create(0, &a, &b));

  ASSERT_OK(a.write(0, "A", 1, nullptr));
  constexpr size_t kSize = 4096;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kSize, 0, &vmo));

  zx_vaddr_t addr;

  // Note, no options means the buffer is not writable.
  ASSERT_OK(zx::vmar::root_self()->map(0, 0, vmo, 0, kSize, &addr));

  size_t actual = 99;
  void* buffer = reinterpret_cast<void*>(addr);
  ASSERT_NE(nullptr, buffer);

  // Will fail because buffer points at memory that isn't writable.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, b.read(0, buffer, 1, &actual));

  // See that it's unmodified.
  //
  // N.B. this test is actually stricter than what is promised by the interface.  The contract
  // does not explicitly promise that |actual| is unmodified on error.  If you find that this test
  // has failed, it does not necessarily indicate a bug.
  EXPECT_EQ(99, actual);
}

TEST(SocketTest, WriteFromNullBuffer) {
  zx::socket a, b;
  ASSERT_OK(zx::socket::create(0, &a, &b));

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, a.write(0, nullptr, 1, nullptr));
}

TEST(SocketTest, WriteFromBadBuffer) {
  zx::socket a, b;
  ASSERT_OK(zx::socket::create(0, &a, &b));

  constexpr size_t kSize = 4096;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kSize, 0, &vmo));

  zx_vaddr_t addr;

  // Note, no options means the buffer is not readable.
  ASSERT_OK(zx::vmar::root_self()->map(0, 0, vmo, 0, kSize, &addr));

  void* buffer = reinterpret_cast<void*>(addr);
  ASSERT_NE(nullptr, buffer);

  // Will fail because buffer points at memory that isn't readable.
  size_t actual;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, b.write(0, buffer, 1, &actual));
}

}  // namespace
