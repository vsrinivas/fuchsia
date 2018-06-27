// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gap/low_energy_advertising_manager.h"

#include <map>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/random/rand.h"
#include "lib/gtest/test_loop_fixture.h"

namespace btlib {

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
  hci::LowEnergyAdvertiser::ConnectionCallback connect_cb;
};

// LowEnergyAdvertiser for testing purposes:
//  - Reports max_ads supported
//  - Reports mas_ad_size supported
//  - Actually just accepts all ads and stores them in ad_store
class FakeLowEnergyAdvertiser final : public hci::LowEnergyAdvertiser {
 public:
  FakeLowEnergyAdvertiser(
      size_t max_ads,
      size_t max_ad_size,
      std::map<common::DeviceAddress, AdvertisementStatus>* ad_store)
      : max_ads_(max_ads), max_ad_size_(max_ad_size), ads_(ad_store) {
    FXL_CHECK(ads_);
  }

  ~FakeLowEnergyAdvertiser() override = default;

  size_t GetSizeLimit() override { return max_ad_size_; }

  size_t GetMaxAdvertisements() const override { return max_ads_; }

  void StartAdvertising(const common::DeviceAddress& address,
                        const common::ByteBuffer& data,
                        const common::ByteBuffer& scan_rsp,
                        ConnectionCallback connect_callback,
                        uint32_t interval_ms,
                        bool anonymous,
                        AdvertisingStatusCallback callback) override {
    if (!pending_error_) {
      callback(0, pending_error_);
      pending_error_ = hci::Status();
      return;
    }
    if (data.size() > max_ad_size_) {
      callback(0, hci::Status(common::HostError::kInvalidParameters));
      return;
    }
    if (scan_rsp.size() > max_ad_size_) {
      callback(0, hci::Status(common::HostError::kInvalidParameters));
      return;
    }
    AdvertisementStatus new_status;
    AdvertisingData::FromBytes(data, &new_status.data);
    AdvertisingData::FromBytes(scan_rsp, &new_status.scan_rsp);
    new_status.connect_cb = std::move(connect_callback);
    new_status.interval_ms = interval_ms;
    new_status.anonymous = anonymous;
    ads_->emplace(address, std::move(new_status));
    callback(interval_ms, hci::Status());
  }

  bool StopAdvertising(const common::DeviceAddress& address) override {
    ads_->erase(address);
    return true;
  }

  void OnIncomingConnection(hci::ConnectionPtr link) override {
    // Right now, we call the first callback, because we can't call any other
    // ones.
    // TODO(jamuraa): make this send it to the correct callback once we can
    // determine which one that is.
    const auto& cb = ads_->begin()->second.connect_cb;
    if (cb) {
      cb(std::move(link));
    }
  }

  // Sets this faker up to send an error back from the next StartAdvertising
  // call. Set to success to disable a previously called error.
  void ErrorOnNext(hci::Status error_status) { pending_error_ = error_status; }

 private:
  size_t max_ads_, max_ad_size_;
  std::map<common::DeviceAddress, AdvertisementStatus>* ads_;
  hci::Status pending_error_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeLowEnergyAdvertiser);
};

using TestingBase = ::gtest::TestLoopFixture;

class GAP_LowEnergyAdvertisingManagerTest : public TestingBase {
 public:
  GAP_LowEnergyAdvertisingManagerTest() = default;
  ~GAP_LowEnergyAdvertisingManagerTest() override = default;

 protected:
  void SetUp() override { MakeFakeAdvertiser(); }

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

  LowEnergyAdvertisingManager::AdvertisingStatusCallback GetErrorCallback() {
    return [this](std::string ad_id, hci::Status status) {
      EXPECT_TRUE(ad_id.empty());
      EXPECT_FALSE(status);
      last_status_ = status;
    };
  }

  LowEnergyAdvertisingManager::AdvertisingStatusCallback GetSuccessCallback() {
    return [this](std::string ad_id, hci::Status status) {
      last_ad_id_ = ad_id;
      EXPECT_FALSE(ad_id.empty());
      EXPECT_TRUE(status);
      last_status_ = status;
    };
  }

  void MakeFakeAdvertiser(size_t max_ads = kDefaultMaxAds,
                          size_t max_ad_size = kDefaultMaxAdSize) {
    advertiser_ = std::make_unique<FakeLowEnergyAdvertiser>(
        max_ads, max_ad_size, &ad_store_);
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

  FakeLowEnergyAdvertiser* advertiser() const { return advertiser_.get(); }

 private:
  std::map<common::DeviceAddress, AdvertisementStatus> ad_store_;
  std::string last_ad_id_;
  common::Optional<hci::Status> last_status_;
  std::unique_ptr<FakeLowEnergyAdvertiser> advertiser_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GAP_LowEnergyAdvertisingManagerTest);
};

// Tests:
//  - When the advertiser succeeds, the callback is called with the success
TEST_F(GAP_LowEnergyAdvertisingManagerTest, Success) {
  LowEnergyAdvertisingManager am(advertiser());
  AdvertisingData fake_ad = CreateFakeAdvertisingData();
  AdvertisingData scan_rsp;  // Empty scan response

  am.StartAdvertising(fake_ad, scan_rsp, nullptr, kTestIntervalMs,
                      false /* anonymous */, GetSuccessCallback());

  RunLoopUntilIdle();

  EXPECT_TRUE(MoveLastStatus());
  EXPECT_EQ(1u, ad_store().size());
}

TEST_F(GAP_LowEnergyAdvertisingManagerTest, DataSize) {
  LowEnergyAdvertisingManager am(advertiser());
  AdvertisingData fake_ad = CreateFakeAdvertisingData();
  AdvertisingData scan_rsp;  // Empty scan response

  am.StartAdvertising(fake_ad, scan_rsp, nullptr, kTestIntervalMs,
                      false /* anonymous */, GetSuccessCallback());

  RunLoopUntilIdle();

  fake_ad = CreateFakeAdvertisingData(kDefaultMaxAdSize + 1);

  EXPECT_TRUE(MoveLastStatus());
  EXPECT_EQ(1u, ad_store().size());

  am.StartAdvertising(fake_ad, scan_rsp, nullptr, kTestIntervalMs,
                      false /* anonymous */, GetErrorCallback());

  RunLoopUntilIdle();

  EXPECT_TRUE(MoveLastStatus());
  EXPECT_EQ(1u, ad_store().size());
}

//  - Stopping one that is registered stops it in the advertiser
//    (and stops the right address)
//  - Stopping an advertisement that isn't registered retuns false
TEST_F(GAP_LowEnergyAdvertisingManagerTest, RegisterUnregister) {
  MakeFakeAdvertiser(2 /* ads available */);
  LowEnergyAdvertisingManager am(advertiser());
  AdvertisingData fake_ad = CreateFakeAdvertisingData();
  AdvertisingData scan_rsp;  // Empty scan response

  EXPECT_FALSE(am.StopAdvertising(""));

  am.StartAdvertising(fake_ad, scan_rsp, nullptr, kTestIntervalMs,
                      false /* anonymous */, GetSuccessCallback());

  RunLoopUntilIdle();

  EXPECT_TRUE(MoveLastStatus());
  EXPECT_EQ(1u, ad_store().size());

  std::string first_ad_id = last_ad_id();
  common::DeviceAddress first_ad_address = ad_store().begin()->first;

  am.StartAdvertising(fake_ad, scan_rsp, nullptr, kTestIntervalMs,
                      false /* anonymous */, GetSuccessCallback());

  RunLoopUntilIdle();

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
  advertiser()->ErrorOnNext(hci::Status(hci::kInvalidHCICommandParameters));
  LowEnergyAdvertisingManager am(advertiser());
  AdvertisingData fake_ad = CreateFakeAdvertisingData();
  AdvertisingData scan_rsp;

  am.StartAdvertising(fake_ad, scan_rsp, nullptr, kTestIntervalMs,
                      false /* anonymous */, GetErrorCallback());

  RunLoopUntilIdle();

  EXPECT_TRUE(MoveLastStatus());
}

//  - It calls the connectable callback correctly when connected to
TEST_F(GAP_LowEnergyAdvertisingManagerTest, ConnectCallback) {
  auto incoming_conn_cb = fit::bind_member(
      advertiser(), &FakeLowEnergyAdvertiser::OnIncomingConnection);
  LowEnergyAdvertisingManager am(advertiser());
  AdvertisingData fake_ad = CreateFakeAdvertisingData();
  AdvertisingData scan_rsp;

  hci::ConnectionPtr link;
  std::string advertised_id = "not-a-uuid";
  bool called = false;

  auto connect_cb = [this, &advertised_id, &called](std::string connected_id,
                                                    hci::ConnectionPtr link) {
    called = true;
    EXPECT_EQ(advertised_id, connected_id);
  };

  am.StartAdvertising(fake_ad, scan_rsp, connect_cb, kTestIntervalMs,
                      false /* anonymous */, GetSuccessCallback());

  RunLoopUntilIdle();

  EXPECT_TRUE(MoveLastStatus());
  advertised_id = last_ad_id();

  incoming_conn_cb(std::move(link));

  RunLoopUntilIdle();
}

//  - Error: Connectable and Anonymous at the same time
TEST_F(GAP_LowEnergyAdvertisingManagerTest, ConnectAdvertiseError) {
  LowEnergyAdvertisingManager am(advertiser());
  AdvertisingData fake_ad = CreateFakeAdvertisingData();
  AdvertisingData scan_rsp;

  auto connect_cb = [this](std::string connected_id, hci::ConnectionPtr conn) { };

  am.StartAdvertising(fake_ad, scan_rsp, connect_cb, kTestIntervalMs,
                      true /* anonymous */, GetErrorCallback());

  EXPECT_TRUE(MoveLastStatus());
}

//  - Passes the values for the data on. (anonymous, data, scan_rsp,
//  interval_ms)
TEST_F(GAP_LowEnergyAdvertisingManagerTest, SendsCorrectData) {
  LowEnergyAdvertisingManager am(advertiser());
  AdvertisingData fake_ad = CreateFakeAdvertisingData();
  AdvertisingData scan_rsp = CreateFakeAdvertisingData(21 /* size of ad */);

  uint32_t interval_ms = (uint32_t)fxl::RandUint64();

  am.StartAdvertising(fake_ad, scan_rsp, nullptr, interval_ms,
                      false /* anonymous */, GetSuccessCallback());

  RunLoopUntilIdle();

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
}  // namespace btlib
