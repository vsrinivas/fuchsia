// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gap/adapter.h"

#include <memory>

#include <zx/channel.h>

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/testing/fake_controller.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace gap {
namespace {

using ::bluetooth::testing::FakeController;

using TestingBase = ::bluetooth::testing::FakeControllerTest<FakeController>;

class AdapterTest : public TestingBase {
 public:
  AdapterTest() = default;
  ~AdapterTest() override = default;

  void SetUp() override {
    transport_closed_called_ = false;

    SetUpTransport();
    FXL_DCHECK(transport());
    adapter_ = std::make_unique<Adapter>(transport());
    test_device()->Start();
  }

  void TearDown() override {
    if (adapter_->IsInitialized())
      adapter_->ShutDown();
    adapter_ = nullptr;
    TestingBase::TearDown();
  }

  void InitializeAdapter(const Adapter::InitializeCallback& callback) {
    adapter_->Initialize(callback, [this] {
      transport_closed_called_ = true;
      message_loop()->QuitNow();
    });
  }

 protected:
  bool transport_closed_called() const { return transport_closed_called_; }

  Adapter* adapter() const { return adapter_.get(); }

 private:
  bool transport_closed_called_;
  std::unique_ptr<Adapter> adapter_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AdapterTest);
};

using GAP_AdapterTest = AdapterTest;

TEST_F(GAP_AdapterTest, InitializeFailureNoFeaturesSupported) {
  bool success;
  int init_cb_count = 0;
  auto init_cb = [&, this](bool cb_success) {
    success = cb_success;
    init_cb_count++;
    message_loop()->QuitNow();
  };

  // The controller supports nothing.
  InitializeAdapter(init_cb);
  RunMessageLoop();
  EXPECT_FALSE(success);
  EXPECT_EQ(1, init_cb_count);
  EXPECT_FALSE(transport_closed_called());
}

TEST_F(GAP_AdapterTest, InitializeFailureNoBufferInfo) {
  bool success;
  int init_cb_count = 0;
  auto init_cb = [&, this](bool cb_success) {
    success = cb_success;
    init_cb_count++;
    message_loop()->QuitNow();
  };

  // Enable LE support.
  FakeController::Settings settings;
  settings.lmp_features_page0 |=
      static_cast<uint64_t>(hci::LMPFeature::kLESupported);
  test_device()->set_settings(settings);

  InitializeAdapter(init_cb);
  RunMessageLoop();
  EXPECT_FALSE(success);
  EXPECT_EQ(1, init_cb_count);
  EXPECT_FALSE(transport_closed_called());
}

TEST_F(GAP_AdapterTest, InitializeSuccess) {
  bool success;
  int init_cb_count = 0;
  auto init_cb = [&, this](bool cb_success) {
    success = cb_success;
    init_cb_count++;
    message_loop()->QuitNow();
  };

  // Return valid buffer information and enable LE support. (This should
  // succeed).
  FakeController::Settings settings;
  settings.lmp_features_page0 |=
      static_cast<uint64_t>(hci::LMPFeature::kLESupported);
  settings.le_acl_data_packet_length = 5;
  settings.le_total_num_acl_data_packets = 1;
  test_device()->set_settings(settings);

  InitializeAdapter(init_cb);
  RunMessageLoop();
  EXPECT_TRUE(success);
  EXPECT_EQ(1, init_cb_count);
  EXPECT_TRUE(adapter()->state().IsLowEnergySupported());
  EXPECT_FALSE(transport_closed_called());
}

TEST_F(GAP_AdapterTest, InitializeFailureHCICommandError) {
  bool success;
  int init_cb_count = 0;
  auto init_cb = [&, this](bool cb_success) {
    success = cb_success;
    init_cb_count++;
    message_loop()->QuitNow();
  };

  // Make all settings valid but make an HCI command fail.
  FakeController::Settings settings;
  settings.ApplyLEOnlyDefaults();
  test_device()->set_settings(settings);
  test_device()->SetDefaultResponseStatus(hci::kLEReadLocalSupportedFeatures,
                                          hci::Status::kHardwareFailure);

  InitializeAdapter(init_cb);
  RunMessageLoop();
  EXPECT_FALSE(success);
  EXPECT_EQ(1, init_cb_count);
  EXPECT_FALSE(adapter()->state().IsLowEnergySupported());
  EXPECT_FALSE(transport_closed_called());
}

TEST_F(GAP_AdapterTest, TransportClosedCallback) {
  bool success;
  int init_cb_count = 0;
  auto init_cb = [&, this](bool cb_success) {
    success = cb_success;
    init_cb_count++;
    message_loop()->QuitNow();
  };

  FakeController::Settings settings;
  settings.ApplyLEOnlyDefaults();
  test_device()->set_settings(settings);

  InitializeAdapter(init_cb);
  RunMessageLoop();
  EXPECT_TRUE(success);
  EXPECT_EQ(1, init_cb_count);
  EXPECT_TRUE(adapter()->state().IsLowEnergySupported());
  EXPECT_FALSE(transport_closed_called());

  // Deleting the FakeController should cause the transport closed callback to
  // get called.
  message_loop()->task_runner()->PostTask([this] { DeleteTestDevice(); });
  RunMessageLoop();
  EXPECT_TRUE(transport_closed_called());
}

}  // namespace
}  // namespace gap
}  // namespace bluetooth
