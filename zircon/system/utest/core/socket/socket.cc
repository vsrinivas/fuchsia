// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/socket.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include <string_view>
#include <utility>

#include <fbl/array.h>
#include <zxtest/zxtest.h>

namespace {

constexpr std::string_view kMsg1 = "12345";
constexpr std::string_view kMsg2 = "abcdef";
constexpr std::string_view kMsg3 = "ghijklm";
constexpr uint32_t kReadBufSize = std::max({kMsg1.size(), kMsg2.size(), kMsg3.size()}) + 1;

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
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  uint32_t data;
  ASSERT_STATUS(local.read(0u, &data, sizeof(data), nullptr), ZX_ERR_SHOULD_WAIT);
}

TEST(SocketTest, WriteReadDataVerify) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  constexpr uint32_t write_data[] = {0xdeadbeef, 0xc0ffee};

  {
    size_t count;
    ASSERT_OK(local.write(0u, &write_data[0], sizeof(write_data[0]), &count));
    ASSERT_EQ(count, sizeof(write_data[0]));
  }
  {
    size_t count;
    ASSERT_OK(local.write(0u, &write_data[1], sizeof(write_data[1]), &count));
    ASSERT_EQ(count, sizeof(write_data[1]));
  }
  {
    size_t count;
    uint32_t read_data[std::size(write_data)];
    ASSERT_OK(remote.read(0u, read_data, sizeof(read_data), &count));
    ASSERT_EQ(count, sizeof(read_data));
    EXPECT_BYTES_EQ(read_data, write_data, sizeof(write_data));
  }

  {
    size_t count;
    ASSERT_OK(local.write(0u, write_data, sizeof(write_data), &count));
    ASSERT_EQ(count, sizeof(write_data));
  }
  {
    size_t count;
    uint32_t read_data[std::size(write_data)];
    ASSERT_OK(remote.read(0u, read_data, sizeof(read_data), &count));
    ASSERT_EQ(count, sizeof(read_data));
    EXPECT_BYTES_EQ(read_data, write_data, sizeof(write_data));
  }
}

TEST(SocketTest, PeerClosedError) {
  zx::socket local;
  {
    zx::socket remote;
    ASSERT_OK(zx::socket::create(0, &local, &remote));
    // remote gets closed here.
  }

  uint32_t data;
  ASSERT_STATUS(local.write(0u, &data, sizeof(data), nullptr), ZX_ERR_PEER_CLOSED);
}

TEST(SocketTest, PeekingLeavesData) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  constexpr uint32_t write_data[] = {0xdeadbeef, 0xc0ffee};

  {
    size_t count;
    ASSERT_OK(local.write(0u, &write_data[0], sizeof(write_data[0]), &count));
    ASSERT_EQ(count, sizeof(write_data[0]));
  }
  {
    size_t count;
    ASSERT_OK(local.write(0u, &write_data[1], sizeof(write_data[1]), &count));
    ASSERT_EQ(count, sizeof(write_data[1]));
  }

  {
    size_t count;
    uint32_t read_data[std::size(write_data)];
    ASSERT_OK(remote.read(ZX_SOCKET_PEEK, read_data, sizeof(read_data), &count));
    ASSERT_EQ(count, sizeof(read_data));
    EXPECT_BYTES_EQ(read_data, write_data, sizeof(write_data));
  }

  // The message should still be pending for h1 to read.
  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE);

  {
    size_t count;
    uint32_t read_data[std::size(write_data)];
    ASSERT_OK(remote.read(0u, read_data, sizeof(read_data), &count));
    ASSERT_EQ(count, sizeof(read_data));
    EXPECT_BYTES_EQ(read_data, write_data, sizeof(write_data));
  }

  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);
}

TEST(SocketTest, PeekingIntoEmpty) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  uint32_t data;
  ASSERT_STATUS(local.read(ZX_SOCKET_PEEK, &data, sizeof(data), nullptr), ZX_ERR_SHOULD_WAIT);
}

TEST(SocketTest, Signals) {
  zx::socket local;

  {
    zx::socket remote;
    ASSERT_OK(zx::socket::create(0, &local, &remote));

    EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
    EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);

    const size_t kAllSize = 128 * 1024;
    const size_t kChunk = kAllSize / 16;
    fbl::Array<char> big_buf(new char[kAllSize], kAllSize);
    ASSERT_NOT_NULL(big_buf.data());
    memset(big_buf.data(), 0x66, kAllSize);

    {
      size_t count;
      ASSERT_OK(local.write(0u, big_buf.data(), kChunk, &count));
      ASSERT_EQ(count, kChunk);
    }

    EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
    EXPECT_EQ(GetSignals(remote), ZX_SOCKET_READABLE | ZX_SOCKET_WRITABLE);

    {
      size_t count;
      ASSERT_OK(remote.read(0u, big_buf.data(), kAllSize, &count));
      ASSERT_EQ(count, kChunk);
    }

    EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
    EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);

    ASSERT_STATUS(local.signal_peer(ZX_SOCKET_WRITABLE, 0u), ZX_ERR_INVALID_ARGS);

    ASSERT_OK(local.signal_peer(0u, ZX_USER_SIGNAL_1));

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
  ASSERT_OK(local.set_property(ZX_PROP_SOCKET_RX_THRESHOLD, &count, sizeof(count)));
  count = 0xefffffff;
  ASSERT_STATUS(local.set_property(ZX_PROP_SOCKET_RX_THRESHOLD, &count, sizeof(count)),
                ZX_ERR_INVALID_ARGS);
  count = 0;
  ASSERT_OK(local.set_property(ZX_PROP_SOCKET_TX_THRESHOLD, &count, sizeof(count)));
  count = 0xefffffff;
  EXPECT_STATUS(local.set_property(ZX_PROP_SOCKET_TX_THRESHOLD, &count, sizeof(count)),
                ZX_ERR_INVALID_ARGS);
}

TEST(SocketTest, SetThreshholdsAndCheckSignals) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  /*
   * In the code below, we are going to trigger the READ threshold
   * signal as soon as 101 bytes are available to read, and trigger
   * the WRITE threshold as long as we have 103 bytes we can write/
   */

  /* Set valid Read/Write thresholds and verify */
  constexpr size_t SOCKET2_SIGNALTEST_RX_THRESHOLD = 101;
  {
    size_t count = SOCKET2_SIGNALTEST_RX_THRESHOLD;
    ASSERT_OK(local.set_property(ZX_PROP_SOCKET_RX_THRESHOLD, &count, sizeof(count)));
  }

  {
    size_t count;
    ASSERT_OK(local.get_property(ZX_PROP_SOCKET_RX_THRESHOLD, &count, sizeof(count)));
    ASSERT_EQ(count, (size_t)SOCKET2_SIGNALTEST_RX_THRESHOLD);
  }

  zx_info_socket_t info;
  memset(&info, 0, sizeof(info));
  ASSERT_OK(remote.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  size_t write_threshold = info.tx_buf_max - (SOCKET2_SIGNALTEST_RX_THRESHOLD + 2);
  ASSERT_OK(
      remote.set_property(ZX_PROP_SOCKET_TX_THRESHOLD, &write_threshold, sizeof(write_threshold)));
  {
    size_t count;
    ASSERT_OK(remote.get_property(ZX_PROP_SOCKET_TX_THRESHOLD, &count, sizeof(count)));
    ASSERT_EQ(count, write_threshold);
  }

  /* Make sure duplicates get the same thresholds ! */
  zx::socket local_clone, remote_clone;
  ASSERT_OK(local.duplicate(ZX_RIGHT_SAME_RIGHTS, &local_clone));
  ASSERT_OK(remote.duplicate(ZX_RIGHT_SAME_RIGHTS, &remote_clone));

  {
    size_t count;
    ASSERT_OK(local_clone.get_property(ZX_PROP_SOCKET_RX_THRESHOLD, &count, sizeof(count)));
    ASSERT_EQ(count, (size_t)SOCKET2_SIGNALTEST_RX_THRESHOLD);
  }
  {
    size_t count;
    ASSERT_OK(remote_clone.get_property(ZX_PROP_SOCKET_TX_THRESHOLD, &count, sizeof(count)));
    ASSERT_EQ(count, write_threshold);
  }

  /* Test starting signal state after setting thresholds */
  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(local_clone), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);
  EXPECT_EQ(GetSignals(remote_clone), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);

  /* Write data and test signals */
  size_t bufsize = SOCKET2_SIGNALTEST_RX_THRESHOLD - 1;
  char buf[bufsize];
  {
    size_t count;
    ASSERT_OK(remote.write(0u, buf, bufsize, &count));
    ASSERT_EQ(count, bufsize);
  }

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
  {
    size_t count;
    ASSERT_OK(remote.write(0u, buf, bufsize, &count));
    ASSERT_EQ(count, bufsize);
  }
  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_READ_THRESHOLD);
  EXPECT_EQ(GetSignals(local_clone),
            ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_READ_THRESHOLD);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);
  EXPECT_EQ(GetSignals(remote_clone), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);

  /*
   * Bump up the read threshold and make sure the READ THRESHOLD signal gets
   * deasserted (and then restore the read threshold back).
   */
  {
    size_t count = SOCKET2_SIGNALTEST_RX_THRESHOLD + 50;
    ASSERT_OK(local.set_property(ZX_PROP_SOCKET_RX_THRESHOLD, &count, sizeof(count)));
  }
  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE);
  EXPECT_EQ(GetSignals(local_clone), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE);
  {
    size_t count = SOCKET2_SIGNALTEST_RX_THRESHOLD;
    ASSERT_OK(local.set_property(ZX_PROP_SOCKET_RX_THRESHOLD, &count, sizeof(count)));
  }
  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_READ_THRESHOLD);
  EXPECT_EQ(GetSignals(local_clone),
            ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_READ_THRESHOLD);

  /*
   * Bump the write threshold way up and make sure the WRITE THRESHOLD signal gets
   * deasserted (and then restore the write threshold back).
   */
  {
    size_t count = info.tx_buf_max - 10;
    ASSERT_OK(remote.set_property(ZX_PROP_SOCKET_TX_THRESHOLD, &count, sizeof(count)));
  }
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(remote_clone), ZX_SOCKET_WRITABLE);
  {
    size_t count = write_threshold;
    ASSERT_OK(remote.set_property(ZX_PROP_SOCKET_TX_THRESHOLD, &count, sizeof(count)));
  }
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);
  EXPECT_EQ(GetSignals(remote_clone), ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_THRESHOLD);
  /*
   * Next write enough data to de-assert WRITE Threshold
   */
  bufsize = write_threshold - (SOCKET2_SIGNALTEST_RX_THRESHOLD + 1);
  fbl::Array<char> buf2(new char[bufsize], bufsize);
  {
    size_t count;
    ASSERT_OK(remote.write(0u, buf2.data(), bufsize, &count));
    ASSERT_EQ(count, bufsize);
  }
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
  {
    size_t count;
    ASSERT_OK(local.read(0u, buf2.data(), bufsize, &count));
    ASSERT_EQ(count, bufsize);
  }
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
  ASSERT_STATUS(local.signal_peer(0u, ZX_USER_SIGNAL_0), ZX_ERR_PEER_CLOSED);
}

TEST(SocketTest, PeerClosedSetProperty) {
  zx::socket local;
  size_t t = 1;
  {
    zx::socket remote;
    ASSERT_OK(zx::socket::create(0, &local, &remote));

    ASSERT_OK(local.set_property(ZX_PROP_SOCKET_TX_THRESHOLD, &t, sizeof(t)));
    // remote closed
  }
  ASSERT_STATUS(local.set_property(ZX_PROP_SOCKET_TX_THRESHOLD, &t, sizeof(t)), ZX_ERR_PEER_CLOSED);
}

TEST(SocketTest, ShutdownWrite) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);

  {
    size_t count;
    ASSERT_OK(remote.write(0u, kMsg1.data(), kMsg1.size(), &count));
    ASSERT_EQ(count, kMsg1.size());
  }

  ASSERT_OK(remote.shutdown(ZX_SOCKET_SHUTDOWN_WRITE));

  EXPECT_EQ(GetSignals(local),
            ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITE_DISABLED);

  {
    size_t count;
    ASSERT_OK(local.write(0u, kMsg2.data(), kMsg2.size(), &count));
    ASSERT_EQ(count, kMsg2.size());
  }

  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_READABLE | ZX_SOCKET_WRITE_DISABLED);

  ASSERT_STATUS(remote.write(0u, kMsg3.data(), kMsg3.size(), nullptr), ZX_ERR_BAD_STATE);

  char rbuf[kReadBufSize];

  {
    size_t count;
    ASSERT_OK(local.read(0u, rbuf, sizeof(rbuf), &count));
    ASSERT_EQ(count, kMsg1.size());
    EXPECT_BYTES_EQ(rbuf, kMsg1.data(), kMsg1.size());
  }

  ASSERT_STATUS(local.read(0u, rbuf, 1u, nullptr), ZX_ERR_BAD_STATE);

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_WRITE_DISABLED);

  {
    size_t count;
    ASSERT_OK(remote.read(0u, rbuf, sizeof(rbuf), &count));
    ASSERT_EQ(count, kMsg2.size());
    EXPECT_BYTES_EQ(rbuf, kMsg2.data(), kMsg2.size());
  }

  local.reset();

  // Calling shutdown after the peer is closed is completely valid.
  ASSERT_OK(remote.shutdown(ZX_SOCKET_SHUTDOWN_READ));

  EXPECT_EQ(GetSignals(remote),
            ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED);

  remote.reset();
}

TEST(SocketTest, ShutdownRead) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);

  {
    size_t count;
    ASSERT_OK(remote.write(0u, kMsg1.data(), kMsg1.size(), &count));
    ASSERT_EQ(count, kMsg1.size());
  }

  ASSERT_OK(local.shutdown(ZX_SOCKET_SHUTDOWN_READ));

  EXPECT_EQ(GetSignals(local),
            ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITE_DISABLED);

  {
    size_t count;
    ASSERT_OK(local.write(0u, kMsg2.data(), kMsg2.size(), &count));
    ASSERT_EQ(count, kMsg2.size());
  }

  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_READABLE | ZX_SOCKET_WRITE_DISABLED);

  ASSERT_STATUS(remote.write(0u, kMsg3.data(), kMsg3.size(), nullptr), ZX_ERR_BAD_STATE);

  char rbuf[kReadBufSize];

  {
    size_t count;
    ASSERT_OK(local.read(0u, rbuf, sizeof(rbuf), &count));
    ASSERT_EQ(count, kMsg1.size());
    EXPECT_BYTES_EQ(rbuf, kMsg1.data(), kMsg1.size());
  }

  ASSERT_STATUS(local.read(0u, rbuf, 1u, nullptr), ZX_ERR_BAD_STATE);
  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_WRITE_DISABLED);

  {
    size_t count;
    ASSERT_OK(remote.read(0u, rbuf, sizeof(rbuf), &count));
    ASSERT_EQ(count, kMsg2.size());
    EXPECT_BYTES_EQ(rbuf, kMsg2.data(), kMsg2.size());
  }
}

TEST(SocketTest, BytesOutstanding) {
  zx::socket local;
  constexpr uint32_t write_data[] = {0xdeadbeef, 0xc0ffee};

  {
    zx::socket remote;
    ASSERT_OK(zx::socket::create(0, &local, &remote));

    {
      uint32_t read_data[std::size(write_data)];
      ASSERT_STATUS(local.read(0u, read_data, sizeof(read_data), nullptr), ZX_ERR_SHOULD_WAIT);
    }

    {
      size_t count;
      ASSERT_OK(local.write(0u, &write_data[0], sizeof(write_data[0]), &count));
      ASSERT_EQ(count, sizeof(write_data[0]));
    }
    {
      size_t count;
      ASSERT_OK(local.write(0u, &write_data[1], sizeof(write_data[1]), &count));
      ASSERT_EQ(count, sizeof(write_data[1]));
    }

    // Check the number of bytes outstanding.
    zx_info_socket_t info;
    memset(&info, 0, sizeof(info));
    ASSERT_OK(remote.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
    EXPECT_EQ(info.rx_buf_available, sizeof(write_data));

    // Check that the prior zx_socket_read call didn't disturb the pending data.
    {
      size_t count;
      uint32_t read_data[std::size(write_data)];
      ASSERT_OK(remote.read(0u, read_data, sizeof(read_data), &count));
      ASSERT_EQ(count, sizeof(read_data));
      EXPECT_BYTES_EQ(read_data, write_data, sizeof(write_data));
    }

    // remote is closed
  }

  ASSERT_STATUS(local.write(0u, &write_data[1], sizeof(write_data[1]), nullptr),
                ZX_ERR_PEER_CLOSED);
}

TEST(SocketTest, ShutdownWriteBytesOutstanding) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
  {
    size_t count;
    ASSERT_OK(remote.write(0u, kMsg1.data(), kMsg1.size(), &count));
    ASSERT_EQ(count, kMsg1.size());
  }

  ASSERT_OK(remote.shutdown(ZX_SOCKET_SHUTDOWN_WRITE));

  EXPECT_EQ(GetSignals(local),
            ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITE_DISABLED);

  {
    size_t count;
    ASSERT_OK(local.write(0u, kMsg2.data(), kMsg2.size(), &count));
    ASSERT_EQ(count, kMsg2.size());
  }

  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_READABLE | ZX_SOCKET_WRITE_DISABLED);

  ASSERT_STATUS(remote.write(0u, kMsg3.data(), kMsg3.size(), nullptr), ZX_ERR_BAD_STATE);

  char rbuf[kReadBufSize];

  zx_info_socket_t info;
  memset(&info, 0, sizeof(info));
  ASSERT_OK(local.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.rx_buf_available, kMsg1.size());

  {
    size_t count;
    ASSERT_OK(local.read(0u, rbuf, sizeof(rbuf), &count));
    ASSERT_EQ(count, kMsg1.size());
    EXPECT_BYTES_EQ(rbuf, kMsg1.data(), kMsg1.size());
  }

  ASSERT_STATUS(local.read(0u, rbuf, sizeof(rbuf), nullptr), ZX_ERR_BAD_STATE);

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_WRITE_DISABLED);

  {
    size_t count;
    ASSERT_OK(remote.read(0u, rbuf, sizeof(rbuf), &count));
    ASSERT_EQ(count, kMsg2.size());
    EXPECT_BYTES_EQ(rbuf, kMsg2.data(), kMsg2.size());
  }
}

TEST(SocketTest, ShutdownReadBytesOutstanding) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);

  {
    size_t count;
    ASSERT_OK(remote.write(0u, kMsg1.data(), kMsg1.size(), &count));
    ASSERT_EQ(count, kMsg1.size());
  }

  ASSERT_OK(local.shutdown(ZX_SOCKET_SHUTDOWN_READ));

  EXPECT_EQ(GetSignals(local),
            ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITE_DISABLED);

  {
    size_t count;
    ASSERT_OK(local.write(0u, kMsg2.data(), kMsg2.size(), &count));
    ASSERT_EQ(count, kMsg2.size());
  }

  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_READABLE | ZX_SOCKET_WRITE_DISABLED);

  ASSERT_STATUS(remote.write(0u, kMsg3.data(), kMsg3.size(), nullptr), ZX_ERR_BAD_STATE);

  char rbuf[kReadBufSize];

  zx_info_socket_t info;
  memset(&info, 0, sizeof(info));
  ASSERT_OK(local.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.rx_buf_available, kMsg1.size());

  {
    size_t count;
    ASSERT_OK(local.read(0u, rbuf, sizeof(rbuf), &count));
    ASSERT_EQ(count, kMsg1.size());
    EXPECT_BYTES_EQ(rbuf, kMsg1.data(), kMsg1.size());
  }

  ASSERT_STATUS(local.read(0u, rbuf, sizeof(rbuf), nullptr), ZX_ERR_BAD_STATE);

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_WRITE_DISABLED);

  {
    size_t count;
    ASSERT_OK(remote.read(0u, rbuf, sizeof(rbuf), &count));
    ASSERT_EQ(count, kMsg2.size());
    EXPECT_BYTES_EQ(rbuf, kMsg2.data(), kMsg2.size());
  }
}

TEST(SocketTest, SetDispositionHandleWithoutRight) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
  EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);

  zx_info_handle_basic_t info;
  memset(&info, 0, sizeof(info));
  EXPECT_OK(local.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  EXPECT_TRUE((info.rights & ZX_RIGHT_MANAGE_SOCKET) != 0);

  zx::socket local_clone;
  ASSERT_OK(local.duplicate(info.rights ^ ZX_RIGHT_MANAGE_SOCKET, &local_clone));

  EXPECT_STATUS(local_clone.set_disposition(ZX_SOCKET_DISPOSITION_WRITE_DISABLED, 0),
                ZX_ERR_ACCESS_DENIED);
  EXPECT_STATUS(local_clone.set_disposition(0, ZX_SOCKET_DISPOSITION_WRITE_DISABLED),
                ZX_ERR_ACCESS_DENIED);
}

TEST(SocketTest, SetDispositionInvalidArgs) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  EXPECT_STATUS(local.set_disposition(
                    ZX_SOCKET_DISPOSITION_WRITE_DISABLED | ZX_SOCKET_DISPOSITION_WRITE_ENABLED, 0),
                ZX_ERR_INVALID_ARGS);
  EXPECT_STATUS(local.set_disposition(
                    0, ZX_SOCKET_DISPOSITION_WRITE_DISABLED | ZX_SOCKET_DISPOSITION_WRITE_ENABLED),
                ZX_ERR_INVALID_ARGS);
  constexpr uint32_t invalid_disposition =
      1337 & ~(ZX_SOCKET_DISPOSITION_WRITE_DISABLED | ZX_SOCKET_DISPOSITION_WRITE_ENABLED);
  EXPECT_STATUS(local.set_disposition(invalid_disposition, 0), ZX_ERR_INVALID_ARGS);
  EXPECT_STATUS(local.set_disposition(0, invalid_disposition), ZX_ERR_INVALID_ARGS);
}

void disable_write_helper(bool disable_local_write, bool disable_remote_write, bool use_shutdown) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  zx_signals_t local_state = ZX_SOCKET_WRITABLE;
  zx_signals_t remote_state = ZX_SOCKET_WRITABLE;
  EXPECT_EQ(GetSignals(local), local_state);
  EXPECT_EQ(GetSignals(remote), remote_state);

  auto write_data = [](zx::socket& endpoint, std::string_view msg) {
    size_t count;
    ASSERT_OK(endpoint.write(0u, msg.data(), msg.size(), &count));
    ASSERT_EQ(count, msg.size());
  };

  // Write some data on endpoints that are about to get their writes disabled. Endpoints that keep
  // their write privilege will be written on later: it confirms that disabling writes on a peer
  // does not prevent the other end from writing data.
  if (disable_local_write) {
    ASSERT_NO_FAILURES(write_data(local, kMsg1));
    remote_state |= ZX_SOCKET_READABLE;
    EXPECT_EQ(GetSignals(remote), remote_state);
  }
  if (disable_remote_write) {
    ASSERT_NO_FAILURES(write_data(remote, kMsg2));
    local_state |= ZX_SOCKET_READABLE;
    EXPECT_EQ(GetSignals(local), local_state);
  }

  // Set the dispositions.
  {
    uint32_t shutdown_mode = 0;
    uint32_t local_disposition = 0;
    uint32_t remote_disposition = 0;
    if (disable_local_write) {
      shutdown_mode |= ZX_SOCKET_SHUTDOWN_WRITE;
      local_disposition = ZX_SOCKET_DISPOSITION_WRITE_DISABLED;
      local_state |= ZX_SOCKET_WRITE_DISABLED;
      local_state ^= ZX_SOCKET_WRITABLE;
      remote_state |= ZX_SOCKET_PEER_WRITE_DISABLED;
    }
    if (disable_remote_write) {
      shutdown_mode |= ZX_SOCKET_SHUTDOWN_READ;
      remote_disposition = ZX_SOCKET_DISPOSITION_WRITE_DISABLED;
      remote_state ^= ZX_SOCKET_WRITABLE;
      remote_state |= ZX_SOCKET_WRITE_DISABLED;
      local_state |= ZX_SOCKET_PEER_WRITE_DISABLED;
    }
    if (use_shutdown) {
      ASSERT_OK(local.shutdown(shutdown_mode));
    } else {
      ASSERT_OK(local.set_disposition(local_disposition, remote_disposition));
    }
    EXPECT_EQ(GetSignals(local), local_state);
    EXPECT_EQ(GetSignals(remote), remote_state);
  }

  // Attempt to write data on both endpoints. It should fail if their writes were disabled.
  {
    auto try_write_data = [write_data](zx::socket& endpoint, zx_signals_t& peer_state,
                                       bool write_is_disabled, std::string_view msg) {
      if (write_is_disabled) {
        ASSERT_STATUS(endpoint.write(0u, msg.data(), msg.size(), nullptr), ZX_ERR_BAD_STATE);
        // Furthermore, writes can't be re-enabled when there is buffered data.
        ASSERT_STATUS(endpoint.set_disposition(ZX_SOCKET_DISPOSITION_WRITE_ENABLED, 0),
                      ZX_ERR_BAD_STATE);
      } else {
        write_data(endpoint, msg);
        peer_state |= ZX_SOCKET_READABLE;
      }
    };
    ASSERT_NO_FAILURES(try_write_data(local, remote_state, disable_local_write, kMsg1));
    EXPECT_EQ(GetSignals(remote), remote_state);
    ASSERT_NO_FAILURES(try_write_data(remote, local_state, disable_remote_write, kMsg2));
    EXPECT_EQ(GetSignals(local), local_state);
  }

  auto read_and_verify_data = [](zx::socket& endpoint, std::string_view msg) {
    char rbuf[kReadBufSize];
    size_t count;
    ASSERT_OK(endpoint.read(0u, rbuf, sizeof(rbuf), &count));
    ASSERT_EQ(count, msg.size());
    ASSERT_BYTES_EQ(rbuf, msg.data(), msg.size());
  };

  // Consume the data of both endpoints. Then try to read more: depending on the disposition of the
  // peer, it should fail one way or another.
  {
    auto consume_data = [read_and_verify_data](zx::socket& endpoint, zx_signals_t& state,
                                               bool peer_write_disabled, std::string_view msg) {
      read_and_verify_data(endpoint, msg);
      state ^= ZX_SOCKET_READABLE;
      zx_status_t expected = peer_write_disabled ? ZX_ERR_BAD_STATE : ZX_ERR_SHOULD_WAIT;
      char rbuf[kReadBufSize];
      ASSERT_STATUS(endpoint.read(0u, rbuf, 1u, nullptr), expected);
    };
    ASSERT_NO_FAILURES(consume_data(local, local_state, disable_remote_write, kMsg2));
    EXPECT_EQ(GetSignals(local), local_state);
    ASSERT_NO_FAILURES(consume_data(remote, remote_state, disable_local_write, kMsg1));
    EXPECT_EQ(GetSignals(remote), remote_state);
  }

  // Enable writes on both endpoints, and confirm that reading/writing works from both ends.
  // Only do this when using set_disposition. Shutdown are non-revertible so this wouldn't
  // work.
  if (!use_shutdown) {
    EXPECT_OK(local.set_disposition(ZX_SOCKET_DISPOSITION_WRITE_ENABLED,
                                    ZX_SOCKET_DISPOSITION_WRITE_ENABLED));
    EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
    EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);

    ASSERT_NO_FAILURES(write_data(local, kMsg2));
    ASSERT_NO_FAILURES(write_data(remote, kMsg3));
    EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE);
    EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE | ZX_SOCKET_READABLE);

    ASSERT_NO_FAILURES(read_and_verify_data(local, kMsg3));
    ASSERT_NO_FAILURES(read_and_verify_data(remote, kMsg2));
    EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
    EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);
  }
}

TEST(SocketTest, DisableWriteLocalWithShutdown) { disable_write_helper(true, false, true); }

TEST(SocketTest, DisableWritePeerWithShutdown) { disable_write_helper(false, true, true); }

TEST(SocketTest, DisableWriteBothEndpointWithShutdown) { disable_write_helper(true, true, true); }

TEST(SocketTest, DisableWriteLocalWithSetDisposition) { disable_write_helper(true, false, false); }

TEST(SocketTest, DisableWritePeerWithSetDisposition) { disable_write_helper(false, true, false); }

TEST(SocketTest, DisableWriteBothEndpointsWithSetDisposition) {
  disable_write_helper(true, true, false);
}

TEST(SocketTest, SetDispositionOfClosedPeerWithBufferedData) {
  zx::socket local;
  {
    zx::socket remote;
    ASSERT_OK(zx::socket::create(0, &local, &remote));

    EXPECT_EQ(GetSignals(local), ZX_SOCKET_WRITABLE);
    EXPECT_EQ(GetSignals(remote), ZX_SOCKET_WRITABLE);

    {
      size_t count;
      EXPECT_OK(remote.write(0u, kMsg1.data(), kMsg1.size(), &count));
      EXPECT_EQ(count, kMsg1.size());
    }

    EXPECT_OK(local.set_disposition(0, ZX_SOCKET_DISPOSITION_WRITE_DISABLED));
    // There is buffered data, so we can't re-enable writes.
    EXPECT_STATUS(local.set_disposition(0, ZX_SOCKET_DISPOSITION_WRITE_ENABLED), ZX_ERR_BAD_STATE);
  }

  // Even though the peer is now closed, there is still buffered data so the writes can't be
  // re-enabled.
  EXPECT_STATUS(local.set_disposition(0, ZX_SOCKET_DISPOSITION_WRITE_ENABLED), ZX_ERR_BAD_STATE);
}

TEST(SocketTest, ShortWrite) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));

  zx_info_socket_t info;
  memset(&info, 0, sizeof(info));
  ASSERT_OK(local.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  const size_t buffer_size = info.rx_buf_max + 1;
  fbl::Array<char> buffer(new char[buffer_size], buffer_size);

  size_t written = ~(size_t)0;  // This should get overwritten by the syscall.
  ASSERT_OK(local.write(0u, buffer.data(), buffer_size, &written));
  ASSERT_LT(written, buffer_size);
}

TEST(SocketTest, Datagram) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));

  {
    size_t count;
    ASSERT_OK(local.write(0u, kMsg1.data(), kMsg1.size(), &count));
    ASSERT_EQ(count, kMsg1.size());
  }

  {
    size_t count;
    ASSERT_OK(local.write(0u, kMsg2.data(), kMsg2.size(), &count));
    ASSERT_EQ(count, kMsg2.size());
  }

  // zircon/kernel/object/include/object/mbuf.h: kPayloadSize ~ 2kb
  static constexpr size_t kLargerThanMBufPayloadSize = 4096;
  uint8_t write_data[kLargerThanMBufPayloadSize];
  for (uint32_t i = 0; i < std::size(write_data); i++) {
    write_data[i] = static_cast<uint8_t>(i);
  }

  {
    size_t count;
    ASSERT_OK(local.write(0u, write_data, sizeof(write_data), &count));
    ASSERT_EQ(count, sizeof(write_data));
  }

  zx_info_socket_t info;
  memset(&info, 0, sizeof(info));
  ASSERT_OK(remote.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.rx_buf_available, kMsg1.size());
  // Read less bytes than in the first datagram, the remaining bytes of the first datagram should
  // be truncated.
  {
    size_t count;
    uint8_t read_data[kMsg1.size()];
    ASSERT_OK(remote.read(0u, read_data, kMsg1.size() - 1, &count));
    ASSERT_EQ(count, kMsg1.size() - 1);
    EXPECT_BYTES_EQ(read_data, kMsg1.substr(0, kMsg1.size() - 1).data(), kMsg1.size() - 1);
  }

  memset(&info, 0, sizeof(info));
  ASSERT_OK(remote.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.rx_buf_available, kMsg2.size());
  {
    size_t count;
    uint8_t read_data[kMsg2.size()];
    ASSERT_OK(remote.read(0u, read_data, sizeof(read_data), &count));
    ASSERT_EQ(count, kMsg2.size());
    EXPECT_BYTES_EQ(read_data, kMsg2.data(), kMsg2.size());
  }

  memset(&info, 0, sizeof(info));
  ASSERT_OK(remote.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.rx_buf_available, sizeof(write_data));
  {
    size_t count;
    uint8_t read_data[kLargerThanMBufPayloadSize];
    ASSERT_OK(remote.read(0u, read_data, sizeof(read_data), &count));
    ASSERT_EQ(count, sizeof(write_data));
    EXPECT_BYTES_EQ(read_data, write_data, sizeof(write_data));
  }

  memset(&info, 0, sizeof(info));
  ASSERT_OK(remote.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.rx_buf_available, 0);
}

TEST(SocketTest, DatagramPeek) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));

  {
    size_t count;
    ASSERT_OK(local.write(0u, kMsg1.data(), kMsg1.size(), &count));
    ASSERT_EQ(count, kMsg1.size());
  }

  {
    size_t count;
    ASSERT_OK(local.write(0u, kMsg2.data(), kMsg2.size(), &count));
    ASSERT_EQ(count, kMsg2.size());
  }

  // Short peek.
  {
    size_t count;
    uint8_t read_data[kMsg1.size()];
    ASSERT_OK(remote.read(ZX_SOCKET_PEEK, read_data, kMsg1.size() - 1, &count));
    ASSERT_EQ(count, kMsg1.size() - 1);
    EXPECT_BYTES_EQ(read_data, kMsg1.data(), kMsg1.size() - 1);
  }

  // Full peek should still see the 1st packet.
  {
    size_t count;
    uint8_t read_data[kMsg1.size()];
    ASSERT_OK(remote.read(ZX_SOCKET_PEEK, read_data, kMsg1.size(), &count));
    ASSERT_EQ(count, kMsg1.size());
    EXPECT_BYTES_EQ(read_data, kMsg1.data(), kMsg1.size());
  }

  // Read and consume the 1st packet.
  {
    size_t count;
    uint8_t read_data[kMsg1.size()];
    ASSERT_OK(remote.read(0u, read_data, kMsg1.size(), &count));
    ASSERT_EQ(count, kMsg1.size());
    EXPECT_BYTES_EQ(read_data, kMsg1.data(), kMsg1.size());
  }

  // Now peek should see the 2nd packet.
  {
    size_t count;
    uint8_t read_data[kMsg2.size()];
    ASSERT_OK(remote.read(ZX_SOCKET_PEEK, read_data, kMsg2.size(), &count));
    ASSERT_EQ(count, kMsg2.size());
    EXPECT_BYTES_EQ(read_data, kMsg2.data(), kMsg2.size());
  }
}

TEST(SocketTest, DatagramPeekEmpty) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));
  char data;
  ASSERT_STATUS(local.read(ZX_SOCKET_PEEK, &data, sizeof(data), nullptr), ZX_ERR_SHOULD_WAIT);
}

TEST(SocketTest, DatagramNoShortWrite) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));

  zx_info_socket_t info;
  memset(&info, 0, sizeof(info));
  ASSERT_OK(remote.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  EXPECT_GT(info.tx_buf_max, 0);

  // Pick a size for a huge datagram, and make sure not to overflow.
  size_t buffer_size = info.tx_buf_max * 2;
  ASSERT_GT(buffer_size, 0);

  fbl::Array<char> buffer(new char[buffer_size]{}, buffer_size);
  ASSERT_NOT_NULL(buffer.data());

  size_t written = ~0u;
  ASSERT_STATUS(local.write(0u, buffer.data(), buffer_size, &written), ZX_ERR_OUT_OF_RANGE);
  // Since the syscall failed, it should not have overwritten this output
  // parameter.
  ASSERT_EQ(written, ~0u);
}

TEST(SocketTest, ZeroSize) {
  zx::socket local, remote;
  ASSERT_OK(zx::socket::create(0, &local, &remote));
  char buffer;

  EXPECT_STATUS(local.read(0, &buffer, 0, nullptr), ZX_ERR_SHOULD_WAIT);
  EXPECT_OK(local.write(0, "a", 1, nullptr));
  EXPECT_OK(remote.read(0, &buffer, 0, nullptr));
  EXPECT_OK(remote.read(0, &buffer, 0, nullptr));

  local.reset();
  remote.reset();
  ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote));

  EXPECT_STATUS(local.read(0, &buffer, 0, nullptr), ZX_ERR_SHOULD_WAIT);
  EXPECT_OK(remote.write(0, "a", 1, nullptr));
  EXPECT_OK(local.read(0, &buffer, 0, nullptr));
  EXPECT_OK(local.read(0, &buffer, 0, nullptr));
}

TEST(SocketTest, ReadIntoNullBuffer) {
  zx::socket a, b;
  ASSERT_OK(zx::socket::create(0, &a, &b));

  ASSERT_OK(a.write(0, "A", 1, nullptr));

  size_t actual;
  ASSERT_STATUS(b.read(0, nullptr, 1, &actual), ZX_ERR_INVALID_ARGS);
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
  ASSERT_STATUS(b.read(0, buffer, 1, &actual), ZX_ERR_INVALID_ARGS);

  // See that it's unmodified.
  //
  // N.B. this test is actually stricter than what is promised by the interface.  The contract
  // does not explicitly promise that |actual| is unmodified on error.  If you find that this test
  // has failed, it does not necessarily indicate a bug.
  ASSERT_EQ(99, actual);
}

TEST(SocketTest, WriteFromNullBuffer) {
  zx::socket a, b;
  ASSERT_OK(zx::socket::create(0, &a, &b));

  ASSERT_STATUS(a.write(0, nullptr, 1, nullptr), ZX_ERR_INVALID_ARGS);
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
  ASSERT_STATUS(b.write(0, buffer, 1, &actual), ZX_ERR_INVALID_ARGS);
}

}  // namespace
