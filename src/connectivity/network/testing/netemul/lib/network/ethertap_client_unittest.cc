// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/ethertap/cpp/fidl.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <src/connectivity/network/testing/netemul/lib/network/ethernet_client.h>
#include <src/connectivity/network/testing/netemul/lib/network/ethertap_client.h>
#include <src/lib/fxl/strings/string_printf.h>

namespace netemul {
namespace testing {

#define TEST_BUFF_SIZE (32)
#define TEST_N_FIFO_BUFS (2)
#define TEST_FIFO_SIZE (2048)
#define TEST_MTU_SIZE (1500)

#define WAIT_FOR_ETH_ONLINE(ethnu)       \
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil( \
      [this]() { return eth(ethnu)->online(); }, zx::sec(5)))

using sys::testing::TestWithEnvironment;
class EthertapClientTest : public TestWithEnvironment {
 public:
  // pushes an interface into local vectors
  void PushInterface(Mac* mac = nullptr) {
    EthertapConfig config(fxl::StringPrintf("etap-%lu", taps_.size()));
    config.mtu = TEST_MTU_SIZE;
    config.options = fuchsia::hardware::ethertap::OPT_TRACE;

    if (mac) {
      memcpy(mac->d, config.mac.d, sizeof(config.mac.d));
    }

    ASSERT_TRUE(config.mac.IsLocallyAdministered());
    fprintf(stderr, "startup with mac %02X:%02X:%02X:%02X:%02X:%02X\n",
            config.mac.d[0], config.mac.d[1], config.mac.d[2], config.mac.d[3],
            config.mac.d[4], config.mac.d[5]);

    auto tap = EthertapClient::Create(config);
    ASSERT_TRUE(tap);
    auto eth = EthernetClientFactory().RetrieveWithMAC(config.mac);
    ASSERT_TRUE(eth);
    bool ok = false;

    EthernetConfig eth_config{.nbufs = TEST_N_FIFO_BUFS,
                              .buff_size = TEST_FIFO_SIZE};
    eth->Setup(eth_config, [&ok](zx_status_t status) {
      ASSERT_EQ(status, ZX_OK);
      ok = true;
    });
    // wait until everything is set up correctly
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&ok]() { return ok; }, zx::sec(2)));
    // save tap interface
    taps_.emplace_back(std::move(tap));
    // save eth interface
    eths_.emplace_back(std::move(eth));
  }

  const std::unique_ptr<EthertapClient>& tap(size_t index = 0) {
    return taps_[index];
  }

  const std::unique_ptr<EthernetClient>& eth(size_t index = 0) {
    return eths_[index];
  }

 private:
  std::vector<std::unique_ptr<EthertapClient>> taps_;
  std::vector<std::unique_ptr<EthernetClient>> eths_;
};

TEST_F(EthertapClientTest, CreateEthertapClient) { PushInterface(); }

TEST_F(EthertapClientTest, EthertapReceive) {
  // create single interface and bring it up
  PushInterface();
  tap()->SetLinkUp(true);
  WAIT_FOR_ETH_ONLINE(0);
  tap()->SetPeerClosedCallback([]() { FAIL() << "ethertap should not close"; });
  bool ok = false;

  // prepare test vector
  uint8_t testSend[TEST_BUFF_SIZE];
  for (int i = 0; i < TEST_BUFF_SIZE; i++) {
    testSend[i] = static_cast<uint8_t>(i & 0xFF);
  }

  // listen for data coming through tap
  tap()->SetPacketCallback([&ok, &testSend](std::vector<uint8_t> data) {
    ASSERT_EQ(data.size(), static_cast<size_t>(TEST_BUFF_SIZE));
    ASSERT_EQ(0, memcmp(&data[0], testSend, data.size()));

    ok = true;
  });

  // push data over ethernet driver a couple of times

  for (int i = 0; i < 3; i++) {
    ok = false;
    ASSERT_EQ(ZX_OK,
              eth()->AcquireAndSend([&testSend](void* buff, uint16_t* len) {
                ASSERT_TRUE(*len >= TEST_BUFF_SIZE);
                *len = TEST_BUFF_SIZE;
                memcpy(buff, testSend, TEST_BUFF_SIZE);
              }));

    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&ok]() { return ok; }, zx::sec(2)));
  }
}

TEST_F(EthertapClientTest, EthertapSend) {
  // create single interface and bring it up
  PushInterface();
  tap()->SetLinkUp(true);
  WAIT_FOR_ETH_ONLINE(0);
  tap()->SetPeerClosedCallback([]() { FAIL() << "ethertap should not close"; });
  bool ok = false;

  // prepare test vector
  uint8_t testSend[TEST_BUFF_SIZE];
  for (int i = 0; i < TEST_BUFF_SIZE; i++) {
    testSend[i] = static_cast<uint8_t>(i & 0xFF);
  }

  // listen for ethernet driver data
  eth()->SetDataCallback([&ok, &testSend](const void* data, size_t len) {
    ASSERT_EQ(0, memcmp(data, testSend, len));
    ok = true;
  });

  // send data through tap a couple of times
  for (int i = 0; i < 3; i++) {
    ok = false;
    ASSERT_EQ(ZX_OK, tap()->Send(testSend, TEST_BUFF_SIZE));
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&ok]() { return ok; }, zx::sec(2)));
  }
}

TEST_F(EthertapClientTest, EthertapLink) {
  // create two ethertap interfaces:
  PushInterface();
  PushInterface();
  // bring both up:
  tap(0)->SetLinkUp(true);
  tap(1)->SetLinkUp(true);
  WAIT_FOR_ETH_ONLINE(0);
  WAIT_FOR_ETH_ONLINE(1);
  auto peerClosed = []() { FAIL() << "ethertap should not close"; };
  tap(0)->SetPeerClosedCallback(peerClosed);
  tap(1)->SetPeerClosedCallback(peerClosed);

  // prepare test vectors
  uint8_t testSendA[TEST_BUFF_SIZE];
  uint8_t testSendB[TEST_BUFF_SIZE];
  for (int i = 0; i < TEST_BUFF_SIZE; i++) {
    testSendA[i] = static_cast<uint8_t>(i & 0xFF);
    testSendB[i] = ~static_cast<uint8_t>(i & 0xFF);
  }

  // all data received on tap(0), push into tap(1)...
  tap(0)->SetPacketCallback(
      [this](std::vector<uint8_t> data) { tap(1)->Send(std::move(data)); });

  // ... and vice-versa
  tap(1)->SetPacketCallback(
      [this](std::vector<uint8_t> data) { tap(0)->Send(std::move(data)); });

  bool eth0Received = false;
  bool eth1Received = false;
  // observe data coming out of ethernet driver and assert test vectors match
  eth(0)->SetDataCallback(
      [&eth0Received, &testSendB](const void* data, size_t len) {
        eth0Received = true;
        ASSERT_EQ(0, memcmp(data, testSendB, len));
      });
  eth(1)->SetDataCallback(
      [&eth1Received, &testSendA](const void* data, size_t len) {
        eth1Received = true;
        ASSERT_EQ(0, memcmp(data, testSendA, len));
      });

  // send test vector data on both interfaces
  ASSERT_EQ(ZX_OK, eth(0)->Send(testSendA, TEST_BUFF_SIZE));
  ASSERT_EQ(ZX_OK, eth(1)->Send(testSendB, TEST_BUFF_SIZE));

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&eth0Received, &eth1Received]() { return eth0Received && eth1Received; },
      zx::sec(2)));
}

TEST_F(EthertapClientTest, EthertapClose) {
  Mac mac;
  PushInterface(&mac);
  bool ok = false;
  eth()->device()->GetStatus([&ok](uint32_t status) { ok = true; });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&ok]() { return ok; }, zx::sec(2)));
  ok = false;
  eth()->SetPeerClosedCallback([&ok]() { ok = true; });
  tap()->Close();
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&ok]() { return ok; }, zx::sec(2)));
}

TEST_F(EthertapClientTest, EthertapDies) {
  PushInterface();
  tap()->SetLinkUp(true);
  WAIT_FOR_ETH_ONLINE(0);
  bool ok = false;
  tap()->SetPeerClosedCallback([&ok]() { ok = true; });
  // use internal ethertap signal to destroy ethertap
  tap()->channel().signal_peer(0u, ZX_USER_SIGNAL_7);
  // and wait for callback to get called
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&ok]() { return ok; }, zx::sec(2)));
}

}  // namespace testing
}  // namespace netemul
