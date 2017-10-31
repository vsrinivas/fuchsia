// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_advertising_manager.h"

#include "garnet/drivers/bluetooth/lib/gap/random_address_generator.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/util.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/random/uuid.h"
#include "lib/fxl/strings/string_printf.h"

namespace bluetooth {
namespace gap {

namespace internal {

class ActiveAdvertisement {
 public:
  explicit ActiveAdvertisement(const common::DeviceAddress& address)
      : address_(address), id_(fxl::GenerateUUID()) {}

  ~ActiveAdvertisement() = default;

  const common::DeviceAddress& address() const { return address_; }
  const std::string& id() const { return id_; }

 private:
  common::DeviceAddress address_;
  std::string id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ActiveAdvertisement);
};

}  // namespace internal

LowEnergyAdvertisingManager::LowEnergyAdvertisingManager(
    std::unique_ptr<LowEnergyAdvertiser> advertiser)
    : task_runner_(fsl::MessageLoop::GetCurrent()->task_runner()),
      advertiser_(std::move(advertiser)),
      weak_ptr_factory_(this) {
  FXL_CHECK(advertiser_);
}

LowEnergyAdvertisingManager::~LowEnergyAdvertisingManager() {
  // Turn off all the advertisements!
  for (const auto& ad : advertisements_) {
    advertiser_->StopAdvertising(ad.second->address());
  }
}

bool LowEnergyAdvertisingManager::StartAdvertising(
    const AdvertisingData& data,
    const AdvertisingData& scan_rsp,
    const ConnectionCallback& connect_callback,
    uint32_t interval_ms,
    bool anonymous,
    const AdvertisingResultCallback& result_callback) {
  // Can't be anonymous and connectable
  if (anonymous && connect_callback) {
    FXL_LOG(WARNING) << "Can't advertise anonymously and connectable!";
    return false;
  }
  // See if there are any advertising slots left
  if (advertisements_.size() >= advertiser_->GetMaxAdvertisements()) {
    task_runner_->PostTask(
        std::bind(result_callback, "", hci::kConnectionLimitExceeded));
    return true;
  }
  // See if the advertisement is within the size limit
  if (data.CalculateBlockSize() > advertiser_->GetSizeLimit()) {
    task_runner_->PostTask(
        std::bind(result_callback, "", hci::kMemoryCapacityExceeded));
    return true;
  }
  // Generate the DeviceAddress and id
  // TODO(jamuraa): Generate resolvable private addresses instead if they're
  // connectable.
  auto address = RandomAddressGenerator::PrivateAddress();
  auto ad_ptr = std::make_unique<internal::ActiveAdvertisement>(address);
  LowEnergyAdvertiser::ConnectionCallback adv_conn_cb;
  if (connect_callback) {
    adv_conn_cb = [this, id = ad_ptr->id(), connect_callback](auto conn_ref) {
      // remove the advertiser because advertising has stopped
      advertisements_.erase(id);
      connect_callback(id, std::move(conn_ref));
    };
  }
  auto result_cb = fxl::MakeCopyable(
      [self = weak_ptr_factory_.GetWeakPtr(), ad_ptr = std::move(ad_ptr),
       result_callback](uint32_t, hci::Status status) mutable {
        if (!self)
          return;
        if (status != hci::kSuccess) {
          result_callback("", status);
          return;
        }
        std::string id = ad_ptr->id();
        self->advertisements_.emplace(id, std::move(ad_ptr));
        result_callback(id, status);
      });
  // Call StartAdvertising, with the callback
  return advertiser_->StartAdvertising(address, data, scan_rsp, adv_conn_cb,
                                       interval_ms, anonymous, result_cb);
}

bool LowEnergyAdvertisingManager::StopAdvertising(
    std::string advertisement_id) {
  auto it = advertisements_.find(advertisement_id);
  if (it == advertisements_.end())
    return false;

  advertiser_->StopAdvertising(it->second->address());

  advertisements_.erase(it);
  return true;
}

}  // namespace gap
}  // namespace bluetooth
