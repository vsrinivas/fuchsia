// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gap/low_energy_advertising_manager.h"

#include <map>

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/testing/test_base.h"

#include "fbl/function.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/random/rand.h"

namespace bluetooth {

namespace gap {

namespace {

constexpr size_t kDefaultMaxAds = 1;
constexpr size_t kDefaultMaxAdSize = 23;

constexpr size_t kDefaultFakeAdSize = 20;

constexpr uint32_t kTestIntervalMs = 1000;

struct AdvertisementStatus {
  AdvertisingData data;
  AdvertisingData scan_rsp;
  bool anonymous;
  uint32_t interval_ms;
  LowEnergyAdvertiser::ConnectionCallback connect_cb;
};

// LowEnergyAdvertiser for testing purposes:
//  - Reports max_ads supported
//  - Reports mas_ad_size supported
//  - Actually just accepts all ads and stores them in ad_store
class FakeLowEnergyAdvertiser final : public LowEnergyAdvertiser {
 public:
  FakeLowEnergyAdvertiser(
      size_t max_ads,
      size_t max_ad_size,
      std::map<common::DeviceAddress, AdvertisementStatus>* ad_store)
      : max_ads_(max_ads),
        max_ad_size_(max_ad_size),
        ads_(ad_store),
        pending_error_(hci::kSuccess) {
    FXL_CHECK(ads_);
  }

  ~FakeLowEnergyAdvertiser() override = default;

  size_t GetSizeLimit() override { return max_ad_size_; }

  size_t GetMaxAdvertisements() const override { return max_ads_; }

  bool StartAdvertising(const common::DeviceAddress& address,
                        const AdvertisingData& data,
                        const AdvertisingData& scan_rsp,
                        const ConnectionCallback& connect_callback,
                        uint32_t interval_ms,
                        bool anonymous,
                        const AdvertisingResultCallback& callback) override {
    if (pending_error_ != hci::kSuccess) {
      callback(0, pending_error_);
      pending_error_ = hci::kSuccess;
      return true;
    }
    AdvertisementStatus new_status;
    data.Copy(&new_status.data);
    scan_rsp.Copy(&new_status.scan_rsp);
    new_status.connect_cb = connect_callback;
    new_status.interval_ms = interval_ms;
    new_status.anonymous = anonymous;
    ads_->emplace(address, std::move(new_status));
    callback(interval_ms, hci::kSuccess);
    return true;
  }

  void StopAdvertising(
      const bluetooth::common::DeviceAddress& address) override {
    ads_->erase(address);
  }

  void OnIncomingConnection(LowEnergyConnectionRefPtr connection) override {
    // Right now, we call the first callback, because we can't call any other
    // ones.
    // TODO(jamuraa): make this send it to the correct callback once we can
    // determine which one that is.
    ConnectionCallback cb = ads_->begin()->second.connect_cb;
    if (cb) {
      cb(std::move(connection));
    }
  }

  // Sets this faker up to send an error back from the next StartAdvertising
  // call. Set to hci::kSuccess to disable a previously called error.
  void ErrorOnNext(hci::Status error_status) { pending_error_ = error_status; }

 private:
  size_t max_ads_, max_ad_size_;
  std::map<common::DeviceAddress, AdvertisementStatus>* ads_;
  hci::Status pending_error_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeLowEnergyAdvertiser);
};

using TestingBase = ::bluetooth::testing::TestBase;

class GAP_LowEnergyAdvertisingManagerTest : public TestingBase {
 public:
  GAP_LowEnergyAdvertisingManagerTest() = default;
  ~GAP_LowEnergyAdvertisingManagerTest() override = default;

 protected:
  void SetUp() override {}

  void TearDown() override {}

  // Makes some fake advertising data of a specific |packed_size|
  AdvertisingData CreateFakeAdvertisingData(
      size_t packed_size = kDefaultFakeAdSize) {
    AdvertisingData result;
    auto buffer = common::CreateStaticByteBuffer(0x00, 0x01, 0x02, 0x03, 0x04,
                                                 0x05, 0x06, 0x07, 0x08);
    size_t bytes_left = packed_size;
    while (bytes_left > 0) {
      // Each field to take 10 bytes total, unless the next header (4 bytes)
      // won't fit. In which case we add enough bytes to finish up.
      size_t data_bytes = bytes_left < 14 ? (bytes_left - 4) : 6;
      result.SetManufacturerData(0xb000 + bytes_left,
                                 buffer.view(0, data_bytes));
      bytes_left = packed_size - result.CalculateBlockSize();
    }
    return result;
  }

  LowEnergyAdvertisingManager::AdvertisingResultCallback GetErrorCallback() {
    return [this](std::string ad_id, hci::Status status) {
      EXPECT_TRUE(ad_id.empty());
      EXPECT_NE(hci::kSuccess, status);
      last_status_ = status;
      message_loop()->PostQuitTask();
    };
  }

  LowEnergyAdvertisingManager::AdvertisingResultCallback GetSuccessCallback() {
    return [this](std::string ad_id, hci::Status status) {
      last_ad_id_ = ad_id;
      EXPECT_FALSE(ad_id.empty());
      EXPECT_EQ(hci::kSuccess, status);
      last_status_ = status;
      message_loop()->PostQuitTask();
    };
  }

  std::unique_ptr<FakeLowEnergyAdvertiser> MakeFakeAdvertiser(
      size_t max_ads = kDefaultMaxAds,
      size_t max_ad_size = kDefaultMaxAdSize) {
    return std::make_unique<FakeLowEnergyAdvertiser>(max_ads, max_ad_size,
                                                     &ad_store_);
  }

  const std::map<common::DeviceAddress, AdvertisementStatus>& ad_store() {
    return ad_store_;
  }
  const std::string& last_ad_id() const { return last_ad_id_; }

  // Returns and clears the last callback status. This resets the state to
  // detect another callback.
  const common::Optional<hci::Status> MoveLastStatus() {
    return std::move(last_status_);
  }

 private:
  std::map<common::DeviceAddress, AdvertisementStatus> ad_store_;
  std::string last_ad_id_;
  common::Optional<hci::Status> last_status_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GAP_LowEnergyAdvertisingManagerTest);
};

// Tests:
//  - Can't advertise more than the advertiser says there are slots
//  - When the advertiser succeeds, the callback is called with the success
TEST_F(GAP_LowEnergyAdvertisingManagerTest, AvailableAdCount) {
  LowEnergyAdvertisingManager am(MakeFakeAdvertiser());
  AdvertisingData fake_ad = CreateFakeAdvertisingData();
  AdvertisingData scan_rsp;  // Empty scan response

  EXPECT_TRUE(am.StartAdvertising(fake_ad, scan_rsp, nullptr, kTestIntervalMs,
                                  false /* anonymous */, GetSuccessCallback()));

  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());
  EXPECT_EQ(1u, ad_store().size());

  EXPECT_TRUE(am.StartAdvertising(fake_ad, scan_rsp, nullptr, kTestIntervalMs,
                                  false /* anonymous */, GetErrorCallback()));

  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());
  EXPECT_EQ(1u, ad_store().size());
}

TEST_F(GAP_LowEnergyAdvertisingManagerTest, DataSize) {
  LowEnergyAdvertisingManager am(MakeFakeAdvertiser());
  AdvertisingData fake_ad = CreateFakeAdvertisingData();
  AdvertisingData scan_rsp;  // Empty scan response

  EXPECT_TRUE(am.StartAdvertising(fake_ad, scan_rsp, nullptr, kTestIntervalMs,
                                  false /* anonymous */, GetSuccessCallback()));

  RunMessageLoop();

  fake_ad = CreateFakeAdvertisingData(kDefaultMaxAdSize + 1);

  EXPECT_TRUE(MoveLastStatus());
  EXPECT_EQ(1u, ad_store().size());

  EXPECT_TRUE(am.StartAdvertising(fake_ad, scan_rsp, nullptr, kTestIntervalMs,
                                  false /* anonymous */, GetErrorCallback()));

  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());
  EXPECT_EQ(1u, ad_store().size());
}

//  - Stopping one that is registered stops it in the advertiser
//    (and stops the right address)
//  - Stopping an advertisement that isn't registered retuns false
TEST_F(GAP_LowEnergyAdvertisingManagerTest, RegisterUnregister) {
  LowEnergyAdvertisingManager am(MakeFakeAdvertiser(2 /* ads available */));
  AdvertisingData fake_ad = CreateFakeAdvertisingData();
  AdvertisingData scan_rsp;  // Empty scan response

  EXPECT_FALSE(am.StopAdvertising(""));

  EXPECT_TRUE(am.StartAdvertising(fake_ad, scan_rsp, nullptr, kTestIntervalMs,
                                  false /* anonymous */, GetSuccessCallback()));

  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());
  EXPECT_EQ(1u, ad_store().size());

  std::string first_ad_id = last_ad_id();
  common::DeviceAddress first_ad_address = ad_store().begin()->first;

  EXPECT_TRUE(am.StartAdvertising(fake_ad, scan_rsp, nullptr, kTestIntervalMs,
                                  false /* anonymous */, GetSuccessCallback()));

  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());
  EXPECT_EQ(2u, ad_store().size());

  EXPECT_TRUE(am.StopAdvertising(first_ad_id));

  EXPECT_EQ(1u, ad_store().size());
  EXPECT_TRUE(ad_store().find(first_ad_address) == ad_store().end());

  EXPECT_FALSE(am.StopAdvertising(first_ad_id));

  EXPECT_EQ(1u, ad_store().size());
}

//  - When the advertiser returns an error, we return an error
TEST_F(GAP_LowEnergyAdvertisingManagerTest, AdvertiserError) {
  auto advertiser = MakeFakeAdvertiser();
  advertiser->ErrorOnNext(hci::kInvalidHCICommandParameters);
  LowEnergyAdvertisingManager am(std::move(advertiser));
  AdvertisingData fake_ad = CreateFakeAdvertisingData();
  AdvertisingData scan_rsp;

  EXPECT_TRUE(am.StartAdvertising(fake_ad, scan_rsp, nullptr, kTestIntervalMs,
                                  false /* anonymous */, GetErrorCallback()));

  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());
}

//  - It calls the connectable callback correctly when connected to
TEST_F(GAP_LowEnergyAdvertisingManagerTest, ConnectCallback) {
  auto advertiser = MakeFakeAdvertiser();
  auto incoming_conn_cb = fbl::BindMember(
      advertiser.get(), &FakeLowEnergyAdvertiser::OnIncomingConnection);
  LowEnergyAdvertisingManager am(std::move(advertiser));
  AdvertisingData fake_ad = CreateFakeAdvertisingData();
  AdvertisingData scan_rsp;

  std::string advertised_id = "not-a-uuid";
  LowEnergyConnectionRefPtr ref;
  bool called = false;

  auto connect_cb = [this, &advertised_id, &called](
                        std::string connected_id,
                        LowEnergyConnectionRefPtr conn) {
    called = true;
    EXPECT_EQ(advertised_id, connected_id);
    PostDelayedQuitTask(fxl::TimeDelta());
  };

  EXPECT_TRUE(am.StartAdvertising(fake_ad, scan_rsp, connect_cb,
                                  kTestIntervalMs, false /* anonymous */,
                                  GetSuccessCallback()));

  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());
  advertised_id = last_ad_id();

  incoming_conn_cb(std::move(ref));

  RunMessageLoop();
}

//  - Error: Connectable and Anonymous at the same time
TEST_F(GAP_LowEnergyAdvertisingManagerTest, ConnectAdvertiseError) {
  LowEnergyAdvertisingManager am(MakeFakeAdvertiser());
  AdvertisingData fake_ad = CreateFakeAdvertisingData();
  AdvertisingData scan_rsp;

  auto connect_cb = [this](std::string connected_id,
                           LowEnergyConnectionRefPtr conn) {
    PostDelayedQuitTask(fxl::TimeDelta());
  };

  EXPECT_FALSE(am.StartAdvertising(fake_ad, scan_rsp, connect_cb,
                                   kTestIntervalMs, true /* anonymous */,
                                   GetErrorCallback()));

  // Callback is not called
  EXPECT_FALSE(MoveLastStatus());
}

//  - Passes the values for the data on. (anonymous, data, scan_rsp,
//  interval_ms)
TEST_F(GAP_LowEnergyAdvertisingManagerTest, SendsCorrectData) {
  auto advertiser = MakeFakeAdvertiser();
  LowEnergyAdvertisingManager am(std::move(advertiser));
  AdvertisingData fake_ad = CreateFakeAdvertisingData();
  AdvertisingData scan_rsp = CreateFakeAdvertisingData(21 /* size of ad */);

  uint32_t interval_ms = (uint32_t)fxl::RandUint64();

  EXPECT_TRUE(am.StartAdvertising(fake_ad, scan_rsp, nullptr, interval_ms,
                                  false /* anonymous */, GetSuccessCallback()));

  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());
  EXPECT_EQ(1u, ad_store().size());

  auto ad_status = &ad_store().begin()->second;

  EXPECT_EQ(fake_ad, ad_status->data);
  EXPECT_EQ(scan_rsp, ad_status->scan_rsp);
  EXPECT_EQ(false, ad_status->anonymous);
  EXPECT_EQ(nullptr, ad_status->connect_cb);
  EXPECT_EQ(interval_ms, ad_status->interval_ms);
}

}  // namespace
}  // namespace gap
}  // namespace bluetooth
