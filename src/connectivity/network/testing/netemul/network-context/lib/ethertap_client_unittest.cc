// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/testing/netemul/network-context/lib/ethertap_client.h"

#include <fuchsia/hardware/ethertap/cpp/fidl.h>
#include <fuchsia/netemul/devmgr/cpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/sys/cpp/service_directory.h>

#include "src/connectivity/network/testing/netemul/network-context/lib/ethernet_client.h"
#include "src/connectivity/network/testing/netemul/network-context/lib/realm_setup.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/lib/testing/predicates/status.h"

namespace netemul {
namespace testing {

#define TEST_BUFF_SIZE (32)
#define TEST_N_FIFO_BUFS (2)
#define TEST_FIFO_SIZE (2048)
#define TEST_MTU_SIZE (1500)

#define WAIT_FOR_ETH_ONLINE(ethnu) \
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([this]() { return eth(ethnu)->online(); }, zx::sec(5)))

class EthertapClientTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    services_ = sys::ServiceDirectory::CreateFromNamespace();

    zx::result result = StartDriverTestRealm();
    ASSERT_OK(result.status_value()) << "driver test realm failed to start";
    devfs_root_.reset(std::move(result.value()));
  }

  zx::result<fidl::InterfaceHandle<fuchsia::netemul::devmgr::IsolatedDevmgr>> GetDevmgr() {
    fidl::InterfaceHandle<fuchsia::netemul::devmgr::IsolatedDevmgr> devmgr;
    fidl::WireResult result =
        fidl::WireCall(devfs_root_.directory())
            ->Clone(fuchsia_io::wire::OpenFlags::kCloneSameRights,
                    fidl::ServerEnd<fuchsia_io::Node>(devmgr.NewRequest().TakeChannel()));
    if (!result.ok()) {
      return zx::error(result.status());
    }
    return zx::ok(std::move(devmgr));
  }

  // pushes an interface into local vectors
  void PushInterface() {
    EthertapConfig config(fxl::StringPrintf("etap-%lu", taps_.size()));
    config.tap_cfg.mtu = TEST_MTU_SIZE;
    config.tap_cfg.options = fuchsia::hardware::ethertap::OPT_TRACE;
    zx::result status = GetDevmgr();
    ASSERT_OK(status.status_value()) << "failed to connect request to /dev";
    config.devfs_root = status.value().TakeChannel();

    ASSERT_TRUE(config.IsMacLocallyAdministered());

    fprintf(stderr, "startup with mac %02X:%02X:%02X:%02X:%02X:%02X\n",
            config.tap_cfg.mac.octets[0], config.tap_cfg.mac.octets[1],
            config.tap_cfg.mac.octets[2], config.tap_cfg.mac.octets[3],
            config.tap_cfg.mac.octets[4], config.tap_cfg.mac.octets[5]);

    fuchsia::hardware::ethernet::MacAddress mac;
    config.tap_cfg.mac.Clone(&mac);
    std::unique_ptr<EthertapClient> tap;
    ASSERT_OK(EthertapClient::Create(std::move(config), &tap));
    status = GetDevmgr();
    ASSERT_OK(status.status_value()) << "failed to connect request to /dev";
    auto eth = EthernetClientFactory(EthernetClientFactory::kDevfsEthernetRoot,
                                     status.value().TakeChannel())
                   .RetrieveWithMAC(mac);
    ASSERT_TRUE(eth);
    bool ok = false;

    EthernetConfig eth_config{.nbufs = TEST_N_FIFO_BUFS, .buff_size = TEST_FIFO_SIZE};
    eth->Setup(eth_config, [&ok](zx_status_t status) {
      ASSERT_OK(status);
      ok = true;
    });
    // wait until everything is set up correctly
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&ok]() { return ok; }, zx::sec(2)));
    // save tap interface
    taps_.emplace_back(std::move(tap));
    // save eth interface
    eths_.emplace_back(std::move(eth));
  }

  const std::unique_ptr<EthertapClient>& tap(size_t index = 0) { return taps_[index]; }

  const std::unique_ptr<EthernetClient>& eth(size_t index = 0) { return eths_[index]; }

 private:
  std::shared_ptr<sys::ServiceDirectory> services_;
  fdio_cpp::FdioCaller devfs_root_;
  std::vector<std::unique_ptr<EthertapClient>> taps_;
  std::vector<std::unique_ptr<EthernetClient>> eths_;
};

TEST_F(EthertapClientTest, CreateEthertapClient) { ASSERT_NO_FATAL_FAILURE(PushInterface()); }

TEST_F(EthertapClientTest, EthertapReceive) {
  // create single interface and bring it up
  ASSERT_NO_FATAL_FAILURE(PushInterface());
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
    ASSERT_EQ(0, memcmp(data.data(), testSend, data.size()));

    ok = true;
  });

  // push data over ethernet driver a couple of times

  for (int i = 0; i < 3; i++) {
    ok = false;
    ASSERT_OK(eth()->AcquireAndSend([&testSend](void* buff, uint16_t* len) {
      ASSERT_TRUE(*len >= TEST_BUFF_SIZE);
      *len = TEST_BUFF_SIZE;
      memcpy(buff, testSend, TEST_BUFF_SIZE);
    }));

    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&ok]() { return ok; }, zx::sec(2)));
  }
}

TEST_F(EthertapClientTest, EthertapSend) {
  // create single interface and bring it up
  ASSERT_NO_FATAL_FAILURE(PushInterface());
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
    ASSERT_OK(tap()->Send(testSend, TEST_BUFF_SIZE));
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&ok]() { return ok; }, zx::sec(2)));
  }
}

TEST_F(EthertapClientTest, EthertapLink) {
  // create two ethertap interfaces:
  ASSERT_NO_FATAL_FAILURE(PushInterface());
  ASSERT_NO_FATAL_FAILURE(PushInterface());
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
  tap(0)->SetPacketCallback([this](std::vector<uint8_t> data) { tap(1)->Send(std::move(data)); });

  // ... and vice-versa
  tap(1)->SetPacketCallback([this](std::vector<uint8_t> data) { tap(0)->Send(std::move(data)); });

  bool eth0Received = false;
  bool eth1Received = false;
  // observe data coming out of ethernet driver and assert test vectors match
  eth(0)->SetDataCallback([&eth0Received, &testSendB](const void* data, size_t len) {
    eth0Received = true;
    ASSERT_EQ(0, memcmp(data, testSendB, len));
  });
  eth(1)->SetDataCallback([&eth1Received, &testSendA](const void* data, size_t len) {
    eth1Received = true;
    ASSERT_EQ(0, memcmp(data, testSendA, len));
  });

  // send test vector data on both interfaces
  ASSERT_OK(eth(0)->Send(testSendA, TEST_BUFF_SIZE));
  ASSERT_OK(eth(1)->Send(testSendB, TEST_BUFF_SIZE));

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&eth0Received, &eth1Received]() { return eth0Received && eth1Received; }, zx::sec(2)));
}

TEST_F(EthertapClientTest, EthertapClose) {
  ASSERT_NO_FATAL_FAILURE(PushInterface());
  bool ok = false;

  eth()->device()->GetStatus(
      [&ok](fuchsia::hardware::ethernet::DeviceStatus status) { ok = true; });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&ok]() { return ok; }, zx::sec(2)));
  ok = false;
  eth()->SetPeerClosedCallback([&ok]() { ok = true; });
  tap()->Close();
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&ok]() { return ok; }, zx::sec(2)));
}

TEST_F(EthertapClientTest, EthertapDies) {
  ASSERT_NO_FATAL_FAILURE(PushInterface());
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
