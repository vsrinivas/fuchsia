// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/lib/gap/low_energy_connection_manager.h"

#include <memory>
#include <vector>

#include "gtest/gtest.h"

#include "apps/bluetooth/lib/gap/remote_device.h"
#include "apps/bluetooth/lib/gap/remote_device_cache.h"
#include "apps/bluetooth/lib/l2cap/channel_manager.h"
#include "apps/bluetooth/lib/testing/fake_controller.h"
#include "apps/bluetooth/lib/testing/fake_device.h"
#include "apps/bluetooth/lib/testing/test_base.h"

#include "lib/fxl/macros.h"

namespace bluetooth {
namespace gap {
namespace {

using ::bluetooth::testing::FakeController;
using ::bluetooth::testing::FakeDevice;

using TestingBase = ::bluetooth::testing::TransportTest<FakeController>;

const common::DeviceAddress kAddress0(common::DeviceAddress::Type::kLEPublic, "00:00:00:00:00:01");
const common::DeviceAddress kAddress1(common::DeviceAddress::Type::kLEPublic, "00:00:00:00:00:02");
const common::DeviceAddress kAddress2(common::DeviceAddress::Type::kBREDR, "00:00:00:00:00:03");

// This must be longer than FakeDevice::kDefaultConnectResponseTimeMs.
constexpr int64_t kTestRequestTimeoutMs = 200;

class LowEnergyConnectionManagerTest : public TestingBase {
 public:
  LowEnergyConnectionManagerTest() = default;
  ~LowEnergyConnectionManagerTest() override = default;

 protected:
  void SetUp() override {
    TestingBase::SetUp();
    TestingBase::InitializeACLDataChannel(hci::DataBufferInfo(),
                                          hci::DataBufferInfo(hci::kMaxACLPayloadSize, 10));

    FakeController::Settings settings;
    settings.ApplyLegacyLEConfig();
    test_device()->set_settings(settings);

    dev_cache_ = std::make_unique<RemoteDeviceCache>();
    l2cap_ = std::make_unique<l2cap::ChannelManager>(transport(), message_loop()->task_runner());
    conn_mgr_ = std::make_unique<LowEnergyConnectionManager>(
        Mode::kLegacy, transport(), dev_cache_.get(), l2cap_.get(), kTestRequestTimeoutMs);

    test_device()->SetConnectionStateCallback(
        std::bind(&LowEnergyConnectionManagerTest::OnConnectionStateChanged, this,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
        message_loop()->task_runner());

    test_device()->Start();
  }

  void TearDown() override {
    if (conn_mgr_) conn_mgr_ = nullptr;
    l2cap_ = nullptr;
    dev_cache_ = nullptr;

    TestingBase::TearDown();
  }

  // Deletes |conn_mgr_|.
  void DeleteConnMgr() { conn_mgr_ = nullptr; }

  RemoteDeviceCache* dev_cache() const { return dev_cache_.get(); }
  LowEnergyConnectionManager* conn_mgr() const { return conn_mgr_.get(); }

  // Addresses of currently connected fake devices.
  using DeviceList = std::unordered_set<common::DeviceAddress>;
  const DeviceList& connected_devices() const { return connected_devices_; }

  // Addresses of devices with a canceled connection attempt.
  const DeviceList& canceled_devices() const { return canceled_devices_; }

  // If set to true, this will quit the message loop whenever the FakeController notifies us of a
  // state change.
  void set_quit_message_loop_on_state_change(bool value) {
    quit_message_loop_on_state_change_ = value;
  }

 private:
  std::unique_ptr<RemoteDeviceCache> dev_cache_;
  std::unique_ptr<l2cap::ChannelManager> l2cap_;
  std::unique_ptr<LowEnergyConnectionManager> conn_mgr_;

  // Called by FakeController on connection events.
  void OnConnectionStateChanged(const common::DeviceAddress& address, bool connected,
                                bool canceled) {
    if (canceled) {
      canceled_devices_.insert(address);
    } else if (connected) {
      FXL_DCHECK(connected_devices_.find(address) == connected_devices_.end());
      connected_devices_.insert(address);
    } else {
      FXL_DCHECK(connected_devices_.find(address) != connected_devices_.end());
      connected_devices_.erase(address);
    }

    if (quit_message_loop_on_state_change_) message_loop()->QuitNow();
  }

  bool quit_message_loop_on_state_change_ = false;
  DeviceList connected_devices_;
  DeviceList canceled_devices_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyConnectionManagerTest);
};

TEST_F(LowEnergyConnectionManagerTest, ConnectUnknownDevice) {
  EXPECT_FALSE(conn_mgr()->Connect("nope", {}));
}

TEST_F(LowEnergyConnectionManagerTest, ConnectClassicDevice) {
  auto* dev = dev_cache()->NewDevice(kAddress2, TechnologyType::kClassic, true, true);
  EXPECT_FALSE(conn_mgr()->Connect(dev->identifier(), {}));
}

TEST_F(LowEnergyConnectionManagerTest, ConnectNonConnectableDevice) {
  auto* dev = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, false, true);
  EXPECT_FALSE(conn_mgr()->Connect(dev->identifier(), {}));
}

// An error is received via the HCI Command cb_status event
TEST_F(LowEnergyConnectionManagerTest, ConnectSingleDeviceErrorStatus) {
  auto* dev = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);

  auto fake_dev = std::make_unique<FakeDevice>(kAddress0);
  fake_dev->set_connect_status(hci::Status::kConnectionFailedToBeEstablished);
  test_device()->AddLEDevice(std::move(fake_dev));

  hci::Status status = hci::Status::kSuccess;
  auto callback = [&status](auto cb_status, auto conn_ref) {
    EXPECT_FALSE(conn_ref);
    status = cb_status;

    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback));

  RunMessageLoop();

  EXPECT_EQ(hci::Status::kConnectionFailedToBeEstablished, status);
}

// LE Connection Complete event reports error
TEST_F(LowEnergyConnectionManagerTest, ConnectSingleDeviceFailure) {
  auto* dev = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);

  auto fake_dev = std::make_unique<FakeDevice>(kAddress0);
  fake_dev->set_connect_response(hci::Status::kConnectionFailedToBeEstablished);
  test_device()->AddLEDevice(std::move(fake_dev));

  hci::Status status = hci::Status::kSuccess;
  auto callback = [&status](auto cb_status, auto conn_ref) {
    EXPECT_FALSE(conn_ref);
    status = cb_status;

    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback));

  RunMessageLoop();

  EXPECT_EQ(hci::Status::kConnectionFailedToBeEstablished, status);
}

TEST_F(LowEnergyConnectionManagerTest, ConnectSingleDeviceTimeout) {
  auto* dev = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);

  // We add no fake devices to cause the request to time out.

  hci::Status status = hci::Status::kSuccess;
  auto callback = [&status](auto cb_status, auto conn_ref) {
    EXPECT_FALSE(conn_ref);
    status = cb_status;

    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback));

  RunMessageLoop();

  EXPECT_EQ(hci::Status::kCommandTimeout, status);
}

// Successful connection to single device
TEST_F(LowEnergyConnectionManagerTest, ConnectSingleDevice) {
  auto* dev = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);
  EXPECT_TRUE(dev->temporary());

  auto fake_dev = std::make_unique<FakeDevice>(kAddress0);
  test_device()->AddLEDevice(std::move(fake_dev));

  hci::Status status = hci::Status::kUnknownCommand;  // any error status will do
  LowEnergyConnectionRefPtr conn_ref;
  auto callback = [&status, &conn_ref](auto cb_status, auto cb_conn_ref) {
    EXPECT_TRUE(cb_conn_ref);
    status = cb_status;
    conn_ref = std::move(cb_conn_ref);
    EXPECT_TRUE(conn_ref->active());

    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  EXPECT_TRUE(connected_devices().empty());
  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback));

  RunMessageLoop();

  EXPECT_EQ(hci::Status::kSuccess, status);
  EXPECT_EQ(1u, connected_devices().size());
  EXPECT_EQ(1u, connected_devices().count(kAddress0));

  ASSERT_TRUE(conn_ref);
  EXPECT_TRUE(conn_ref->active());
  EXPECT_EQ(dev->identifier(), conn_ref->device_identifier());
  EXPECT_FALSE(dev->temporary());
}

TEST_F(LowEnergyConnectionManagerTest, ReleaseRef) {
  auto* dev = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);

  auto fake_dev = std::make_unique<FakeDevice>(kAddress0);
  test_device()->AddLEDevice(std::move(fake_dev));

  hci::Status status = hci::Status::kUnknownCommand;  // any error cb_status will do
  LowEnergyConnectionRefPtr conn_ref;
  auto callback = [&status, &conn_ref](auto cb_status, auto cb_conn_ref) {
    EXPECT_TRUE(cb_conn_ref);
    status = cb_status;
    conn_ref = std::move(cb_conn_ref);

    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  EXPECT_TRUE(connected_devices().empty());
  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback));

  RunMessageLoop();

  EXPECT_EQ(hci::Status::kSuccess, status);
  EXPECT_EQ(1u, connected_devices().size());

  ASSERT_TRUE(conn_ref);
  conn_ref = nullptr;

  set_quit_message_loop_on_state_change(true);
  RunMessageLoop();

  EXPECT_TRUE(connected_devices().empty());
}

TEST_F(LowEnergyConnectionManagerTest, OneDeviceTwoPendingRequestsBothFail) {
  constexpr int kRequestCount = 2;

  auto* dev = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);

  auto fake_dev = std::make_unique<FakeDevice>(kAddress0);
  fake_dev->set_connect_response(hci::Status::kConnectionFailedToBeEstablished);
  test_device()->AddLEDevice(std::move(fake_dev));

  hci::Status status[kRequestCount];
  memset(&status, hci::Status::kSuccess, sizeof(status));

  int cb_count = 0;
  auto callback = [&status, &cb_count](auto cb_status, auto conn_ref) {
    EXPECT_FALSE(conn_ref);
    status[cb_count++] = cb_status;

    if (cb_count == kRequestCount) fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  for (int i = 0; i < kRequestCount; ++i) {
    EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback)) << "request count: " << i + 1;
  }

  RunMessageLoop();

  ASSERT_EQ(kRequestCount, cb_count);
  for (int i = 0; i < kRequestCount; ++i) {
    EXPECT_EQ(hci::Status::kConnectionFailedToBeEstablished, status[i])
        << "request count: " << i + 1;
  }
}

TEST_F(LowEnergyConnectionManagerTest, OneDeviceManyPendingRequests) {
  constexpr size_t kRequestCount = 50;

  auto* dev = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);
  auto fake_dev = std::make_unique<FakeDevice>(kAddress0);
  test_device()->AddLEDevice(std::move(fake_dev));

  std::vector<LowEnergyConnectionRefPtr> conn_refs;
  auto callback = [&conn_refs](auto cb_status, auto conn_ref) {
    EXPECT_TRUE(conn_ref);
    EXPECT_EQ(hci::Status::kSuccess, cb_status);
    conn_refs.emplace_back(std::move(conn_ref));

    if (conn_refs.size() == kRequestCount) fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  for (size_t i = 0; i < kRequestCount; ++i) {
    EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback)) << "request count: " << i + 1;
  }

  RunMessageLoop();

  EXPECT_EQ(1u, connected_devices().size());
  EXPECT_EQ(1u, connected_devices().count(kAddress0));

  EXPECT_EQ(kRequestCount, conn_refs.size());
  for (size_t i = 0; i < kRequestCount; ++i) {
    ASSERT_TRUE(conn_refs[i]);
    EXPECT_TRUE(conn_refs[i]->active());
    EXPECT_EQ(dev->identifier(), conn_refs[i]->device_identifier());
  }

  // Release one reference. The rest should be active.
  conn_refs[0] = nullptr;
  for (size_t i = 1; i < kRequestCount; ++i) EXPECT_TRUE(conn_refs[i]->active());

  // Release all but one reference.
  for (size_t i = 1; i < kRequestCount - 1; ++i) conn_refs[i] = nullptr;
  EXPECT_TRUE(conn_refs[kRequestCount - 1]->active());

  // Drop the last reference.
  conn_refs[kRequestCount - 1] = nullptr;

  set_quit_message_loop_on_state_change(true);
  RunMessageLoop();

  EXPECT_TRUE(connected_devices().empty());
}

TEST_F(LowEnergyConnectionManagerTest, AddRefAfterConnection) {
  constexpr size_t kRefCount = 50;

  auto* dev = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);
  auto fake_dev = std::make_unique<FakeDevice>(kAddress0);
  test_device()->AddLEDevice(std::move(fake_dev));

  std::vector<LowEnergyConnectionRefPtr> conn_refs;
  auto callback = [&conn_refs](auto cb_status, auto conn_ref) {
    EXPECT_TRUE(conn_ref);
    EXPECT_EQ(hci::Status::kSuccess, cb_status);
    conn_refs.emplace_back(std::move(conn_ref));

    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback));

  RunMessageLoop();

  EXPECT_EQ(1u, connected_devices().size());
  EXPECT_EQ(1u, connected_devices().count(kAddress0));
  EXPECT_EQ(1u, conn_refs.size());

  // Add new references.
  for (size_t i = 1; i < kRefCount; ++i) {
    EXPECT_TRUE(conn_mgr()->Connect(dev->identifier(), callback)) << "request count: " << i + 1;
    RunMessageLoop();
  }

  EXPECT_EQ(1u, connected_devices().size());
  EXPECT_EQ(1u, connected_devices().count(kAddress0));
  EXPECT_EQ(kRefCount, conn_refs.size());

  // Disconnect.
  conn_refs.clear();

  set_quit_message_loop_on_state_change(true);
  RunMessageLoop();

  EXPECT_TRUE(connected_devices().empty());
}

TEST_F(LowEnergyConnectionManagerTest, PendingRequestsOnTwoDevices) {
  auto* dev0 = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);
  auto* dev1 = dev_cache()->NewDevice(kAddress1, TechnologyType::kLowEnergy, true, true);

  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress1));

  std::vector<LowEnergyConnectionRefPtr> conn_refs;
  auto callback = [&conn_refs](auto cb_status, auto conn_ref) {
    EXPECT_TRUE(conn_ref);
    EXPECT_EQ(hci::Status::kSuccess, cb_status);
    conn_refs.emplace_back(std::move(conn_ref));

    if (conn_refs.size() == 2) fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), callback));
  EXPECT_TRUE(conn_mgr()->Connect(dev1->identifier(), callback));

  RunMessageLoop();

  EXPECT_EQ(2u, connected_devices().size());
  EXPECT_EQ(1u, connected_devices().count(kAddress0));
  EXPECT_EQ(1u, connected_devices().count(kAddress1));

  ASSERT_EQ(2u, conn_refs.size());
  EXPECT_EQ(dev0->identifier(), conn_refs[0]->device_identifier());
  EXPECT_EQ(dev1->identifier(), conn_refs[1]->device_identifier());

  // |dev1| should disconnect first.
  conn_refs[1] = nullptr;

  set_quit_message_loop_on_state_change(true);

  RunMessageLoop();
  EXPECT_EQ(1u, connected_devices().size());
  EXPECT_EQ(1u, connected_devices().count(kAddress0));

  conn_refs.clear();

  RunMessageLoop();
  EXPECT_TRUE(connected_devices().empty());
}

TEST_F(LowEnergyConnectionManagerTest, PendingRequestsOnTwoDevicesOneFails) {
  auto* dev0 = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);
  auto* dev1 = dev_cache()->NewDevice(kAddress1, TechnologyType::kLowEnergy, true, true);

  auto fake_dev0 = std::make_unique<FakeDevice>(kAddress0);
  fake_dev0->set_connect_response(hci::Status::kConnectionFailedToBeEstablished);
  test_device()->AddLEDevice(std::move(fake_dev0));
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress1));

  hci::Status status[2];
  std::vector<LowEnergyConnectionRefPtr> conn_refs;
  auto callback = [&conn_refs, &status](auto cb_status, auto conn_ref) {
    status[conn_refs.size()] = cb_status;
    conn_refs.emplace_back(std::move(conn_ref));

    if (conn_refs.size() == 2) fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), callback));
  EXPECT_TRUE(conn_mgr()->Connect(dev1->identifier(), callback));

  RunMessageLoop();

  EXPECT_EQ(1u, connected_devices().size());
  EXPECT_EQ(1u, connected_devices().count(kAddress1));

  ASSERT_EQ(2u, conn_refs.size());
  EXPECT_FALSE(conn_refs[0]);
  ASSERT_TRUE(conn_refs[1]);
  EXPECT_EQ(dev1->identifier(), conn_refs[1]->device_identifier());

  // Both connections should disconnect.
  conn_refs.clear();

  set_quit_message_loop_on_state_change(true);
  RunMessageLoop();
  EXPECT_TRUE(connected_devices().empty());
}

TEST_F(LowEnergyConnectionManagerTest, Destructor) {
  auto* dev0 = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);
  auto* dev1 = dev_cache()->NewDevice(kAddress1, TechnologyType::kLowEnergy, true, true);

  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress1));

  // Below we create one connection and one pending request to have at the time of destruction.

  LowEnergyConnectionRefPtr conn_ref;
  auto success_cb = [&conn_ref, this](auto status, auto cb_conn_ref) {
    EXPECT_TRUE(cb_conn_ref);
    EXPECT_EQ(hci::Status::kSuccess, status);

    conn_ref = std::move(cb_conn_ref);
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), success_cb));
  RunMessageLoop();

  ASSERT_TRUE(conn_ref);
  bool conn_closed = false;
  conn_ref->set_closed_callback([&conn_closed] { conn_closed = true; });

  bool error_cb_called = false;
  auto error_cb = [&error_cb_called](auto status, auto conn_ref) {
    EXPECT_FALSE(conn_ref);
    EXPECT_EQ(hci::Status::kHardwareFailure, status);
    error_cb_called = true;
  };

  // This request will remain pending.
  EXPECT_TRUE(conn_mgr()->Connect(dev1->identifier(), error_cb));

  // The message loop will be stopped by OnConnectionStateChanged().
  message_loop()->task_runner()->PostTask([this] {
    // This will synchronously notify |conn_ref|'s closed callback so it is expected to execute
    // before the test harness receives the connection callback. Thus it is OK to quit the message
    // loop in the connection callback.
    DeleteConnMgr();
  });

  set_quit_message_loop_on_state_change(true);
  RunMessageLoop();

  EXPECT_TRUE(error_cb_called);
  EXPECT_TRUE(conn_closed);
  EXPECT_EQ(1u, canceled_devices().size());
  EXPECT_EQ(1u, canceled_devices().count(kAddress1));
}

TEST_F(LowEnergyConnectionManagerTest, DisconnectError) {
  auto* dev0 = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  // This should fail as |dev0| is not connected.
  EXPECT_FALSE(conn_mgr()->Disconnect(dev0->identifier()));
}

TEST_F(LowEnergyConnectionManagerTest, Disconnect) {
  auto* dev0 = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);

  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  int closed_count = 0;
  auto closed_cb = [&closed_count] { closed_count++; };

  std::vector<LowEnergyConnectionRefPtr> conn_refs;
  auto success_cb = [&conn_refs, &closed_cb, this](auto status, auto conn_ref) {
    EXPECT_EQ(hci::Status::kSuccess, status);
    ASSERT_TRUE(conn_ref);
    conn_ref->set_closed_callback(closed_cb);
    conn_refs.push_back(std::move(conn_ref));
    if (conn_refs.size() == 2) fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  // Issue two connection refs.
  EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), success_cb));
  EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), success_cb));
  RunMessageLoop();

  ASSERT_EQ(2u, conn_refs.size());

  EXPECT_TRUE(conn_mgr()->Disconnect(dev0->identifier()));

  set_quit_message_loop_on_state_change(true);
  RunMessageLoop();

  EXPECT_EQ(2, closed_count);
  EXPECT_TRUE(connected_devices().empty());
  EXPECT_TRUE(canceled_devices().empty());
}

// Tests when a link is lost without explicitly disconnecting
TEST_F(LowEnergyConnectionManagerTest, DisconnectEvent) {
  auto* dev0 = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);

  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  int closed_count = 0;
  auto closed_cb = [&closed_count, this] {
    closed_count++;
    if (closed_count == 2) fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  std::vector<LowEnergyConnectionRefPtr> conn_refs;
  auto success_cb = [&conn_refs, &closed_cb, this](auto status, auto conn_ref) {
    EXPECT_EQ(hci::Status::kSuccess, status);
    ASSERT_TRUE(conn_ref);
    conn_ref->set_closed_callback(closed_cb);
    conn_refs.push_back(std::move(conn_ref));
    if (conn_refs.size() == 2) fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  // Issue two connection refs.
  EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), success_cb));
  EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), success_cb));
  RunMessageLoop();

  ASSERT_EQ(2u, conn_refs.size());

  // This makes FakeController send us HCI Disconnection Complete events.
  test_device()->Disconnect(kAddress0);

  // The loop will run until |closed_cb| is called twice.
  RunMessageLoop();

  EXPECT_EQ(2, closed_count);
}

TEST_F(LowEnergyConnectionManagerTest, DisconnectWhileRefPending) {
  auto* dev0 = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  LowEnergyConnectionRefPtr conn_ref;
  auto success_cb = [&conn_ref, this](auto status, auto cb_conn_ref) {
    EXPECT_EQ(hci::Status::kSuccess, status);
    ASSERT_TRUE(cb_conn_ref);
    EXPECT_TRUE(cb_conn_ref->active());

    conn_ref = std::move(cb_conn_ref);
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), success_cb));
  RunMessageLoop();
  ASSERT_TRUE(conn_ref);

  auto ref_cb = [](auto status, auto conn_ref) {
    ASSERT_FALSE(conn_ref);
    ASSERT_EQ(hci::Status::kConnectionFailedToBeEstablished, status);
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), ref_cb));

  // This should invalidate the ref that was bound to |ref_cb|.
  EXPECT_TRUE(conn_mgr()->Disconnect(dev0->identifier()));

  RunMessageLoop();
}

// This tests that a connection reference callback returns nullptr if a HCI Disconnection Complete
// event is received for the corresponding ACL link BEFORE the callback gets run.
TEST_F(LowEnergyConnectionManagerTest, DisconnectEventWhileRefPending) {
  auto* dev0 = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  LowEnergyConnectionRefPtr conn_ref;
  auto success_cb = [&conn_ref, this](auto status, auto cb_conn_ref) {
    ASSERT_TRUE(cb_conn_ref);
    ASSERT_EQ(hci::Status::kSuccess, status);
    EXPECT_TRUE(cb_conn_ref->active());

    conn_ref = std::move(cb_conn_ref);
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), success_cb));
  RunMessageLoop();
  ASSERT_TRUE(conn_ref);

  // Request a new reference. Disconnect the link before the reference is received.
  auto ref_cb = [](auto status, auto conn_ref) {
    EXPECT_FALSE(conn_ref);
    EXPECT_EQ(hci::Status::kConnectionFailedToBeEstablished, status);
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  auto disconn_cb = [this, ref_cb, dev0](auto) {
    // The link is gone but conn_mgr() hasn't updated the connection state yet. The request to
    // connect will attempt to add a new reference which will be invalidated before |ref_cb| gets
    // called.
    EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), ref_cb));
  };
  conn_mgr()->SetDisconnectCallbackForTesting(disconn_cb);

  test_device()->Disconnect(kAddress0);
  RunMessageLoop();
}

// Listener receives local initiated connection ref.
TEST_F(LowEnergyConnectionManagerTest, Listener) {
  auto* dev0 = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  LowEnergyConnectionRefPtr conn_ref;
  auto listener = [&conn_ref](auto cb_conn_ref) {
    ASSERT_TRUE(cb_conn_ref);
    EXPECT_TRUE(cb_conn_ref->active());

    conn_ref = std::move(cb_conn_ref);
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  conn_mgr()->AddListener(listener);
  EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), [](auto, auto) {}));
  RunMessageLoop();
  ASSERT_TRUE(conn_ref);
  EXPECT_EQ(dev0->identifier(), conn_ref->device_identifier());

  conn_ref = nullptr;
  set_quit_message_loop_on_state_change(true);
  RunMessageLoop();
  EXPECT_TRUE(connected_devices().empty());
}

// Listener receives local initiated connection ref but does not take its ownership.
TEST_F(LowEnergyConnectionManagerTest, ListenerRefUnclaimed) {
  auto* dev0 = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  bool listener_called = false;
  auto listener = [&listener_called](auto conn_ref) {
    ASSERT_TRUE(conn_ref);
    EXPECT_TRUE(conn_ref->active());

    listener_called = true;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  conn_mgr()->AddListener(listener);
  EXPECT_TRUE(conn_mgr()->Connect(dev0->identifier(), [](auto, auto) {}));

  RunMessageLoop();
  EXPECT_TRUE(listener_called);
  EXPECT_EQ(1u, connected_devices().size());

  set_quit_message_loop_on_state_change(true);
  RunMessageLoop();
  EXPECT_TRUE(connected_devices().empty());
}

// Listener receives remote initiated connection ref.
TEST_F(LowEnergyConnectionManagerTest, ListenerRemoteInitiated) {
  auto* dev0 = dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  LowEnergyConnectionRefPtr conn_ref;
  auto listener = [&conn_ref](auto cb_conn_ref) {
    ASSERT_TRUE(cb_conn_ref);
    EXPECT_TRUE(cb_conn_ref->active());

    conn_ref = std::move(cb_conn_ref);
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  conn_mgr()->AddListener(listener);
  test_device()->ConnectLowEnergy(kAddress0);

  RunMessageLoop();
  ASSERT_TRUE(conn_ref);
  EXPECT_EQ(dev0->identifier(), conn_ref->device_identifier());

  conn_ref = nullptr;
  set_quit_message_loop_on_state_change(true);
  RunMessageLoop();
  EXPECT_TRUE(connected_devices().empty());
}

// Listener receives remote initiated connection ref but does not take its ownership.
TEST_F(LowEnergyConnectionManagerTest, ListenerRemoteInitiatedRefUnclaimed) {
  dev_cache()->NewDevice(kAddress0, TechnologyType::kLowEnergy, true, true);
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  bool listener_called = false;
  auto listener = [&listener_called](auto conn_ref) {
    ASSERT_TRUE(conn_ref);
    EXPECT_TRUE(conn_ref->active());

    listener_called = true;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  // As |listener| does not claim the ref, we expect the test device to eventually disconnect on its
  // own.
  conn_mgr()->AddListener(listener);
  test_device()->ConnectLowEnergy(kAddress0);

  RunMessageLoop();
  EXPECT_TRUE(listener_called);
  EXPECT_EQ(1u, connected_devices().size());

  set_quit_message_loop_on_state_change(true);
  RunMessageLoop();
  EXPECT_TRUE(connected_devices().empty());
}

// Listener receives remote initiated connection ref from a peer that was not seen before.
TEST_F(LowEnergyConnectionManagerTest, ListenerRemoteInitiatedFromUnknownDevice) {
  // Set up a fake device but don't add it to the device cache.
  test_device()->AddLEDevice(std::make_unique<FakeDevice>(kAddress0));

  LowEnergyConnectionRefPtr conn_ref;
  auto listener = [&conn_ref](auto cb_conn_ref) {
    ASSERT_TRUE(cb_conn_ref);
    EXPECT_TRUE(cb_conn_ref->active());

    conn_ref = std::move(cb_conn_ref);
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  conn_mgr()->AddListener(listener);
  test_device()->ConnectLowEnergy(kAddress0);

  RunMessageLoop();
  ASSERT_TRUE(conn_ref);

  // There should be a matching device in the cache now.
  auto dev = dev_cache()->FindDeviceById(conn_ref->device_identifier());
  ASSERT_TRUE(dev);
  EXPECT_EQ(kAddress0, dev->address());
  EXPECT_EQ(TechnologyType::kLowEnergy, dev->technology());
  EXPECT_TRUE(dev->connectable());

  conn_ref = nullptr;
  set_quit_message_loop_on_state_change(true);
  RunMessageLoop();
  EXPECT_TRUE(connected_devices().empty());
}

}  // namespace
}  // namespace gap
}  // namespace bluetooth
