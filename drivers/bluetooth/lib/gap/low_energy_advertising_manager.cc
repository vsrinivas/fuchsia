// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_advertising_manager.h"

#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "garnet/drivers/bluetooth/lib/gap/random_address_generator.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/util.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/random/uuid.h"
#include "lib/fxl/strings/string_printf.h"

namespace btlib {
namespace gap {

namespace {

constexpr size_t kFlagsSize = 3;
constexpr uint8_t kDefaultFlags = 0;

// Write the block for the flags to the |buffer|.
void WriteFlags(common::MutableByteBuffer* buffer, bool limited = false) {
  FXL_CHECK(buffer->size() >= kFlagsSize);
  (*buffer)[0] = 2;
  (*buffer)[1] = static_cast<uint8_t>(DataType::kFlags);
  if (limited) {
    (*buffer)[2] = kDefaultFlags | AdvFlag::kLELimitedDiscoverableMode;
  } else {
    (*buffer)[2] = kDefaultFlags | AdvFlag::kLEGeneralDiscoverableMode;
  }
}

}  // namespace

class LowEnergyAdvertisingManager::ActiveAdvertisement final {
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

LowEnergyAdvertisingManager::LowEnergyAdvertisingManager(
    hci::LowEnergyAdvertiser* advertiser)
    : advertiser_(advertiser), weak_ptr_factory_(this) {
  FXL_CHECK(advertiser_);
}

LowEnergyAdvertisingManager::~LowEnergyAdvertisingManager() {
  // Turn off all the advertisements!
  for (const auto& ad : advertisements_) {
    advertiser_->StopAdvertising(ad.second->address());
  }
}

void LowEnergyAdvertisingManager::StartAdvertising(
    const AdvertisingData& data,
    const AdvertisingData& scan_rsp,
    ConnectionCallback connect_callback,
    uint32_t interval_ms,
    bool anonymous,
    AdvertisingStatusCallback status_callback) {
  // Can't be anonymous and connectable
  if (anonymous && connect_callback) {
    FXL_LOG(WARNING) << "Can't advertise anonymously and connectable!";
    status_callback("", hci::Status(common::HostError::kInvalidParameters));
    return;
  }
  // Generate the DeviceAddress and id
  // TODO(armansito): Extract private address generation to a centrally
  // accessible location and persist the address so that it can be used for
  // pairing.
  auto address = RandomAddressGenerator::PrivateAddress();
  auto ad_ptr = std::make_unique<ActiveAdvertisement>(address);
  auto self = weak_ptr_factory_.GetWeakPtr();

  hci::LowEnergyAdvertiser::ConnectionCallback adv_conn_cb;
  if (connect_callback) {
    adv_conn_cb = [self, id = ad_ptr->id(), connect_callback = std::move(connect_callback)](auto link) {
      FXL_VLOG(1)
          << "gap: LowEnergyAdvertisingManager: received new connection.";
      if (!self)
        return;

      // remove the advertiser because advertising has stopped
      self->advertisements_.erase(id);
      connect_callback(id, std::move(link));
    };
  }
  auto status_cb = [self, ad_ptr = std::move(ad_ptr),
                    status_callback = std::move(status_callback)](
                       uint32_t, hci::Status status) mutable {
    if (!self)
      return;

    if (!status) {
      status_callback("", status);
      return;
    }

    const std::string& id = ad_ptr->id();
    self->advertisements_.emplace(id, std::move(ad_ptr));
    status_callback(id, status);
  };

  // Serialize the data
  auto data_block =
      common::NewSlabBuffer(data.CalculateBlockSize() + kFlagsSize);
  WriteFlags(data_block.get());
  auto data_view = data_block->mutable_view(kFlagsSize);
  data.WriteBlock(&data_view);

  auto scan_rsp_block = common::NewSlabBuffer(scan_rsp.CalculateBlockSize());
  scan_rsp.WriteBlock(scan_rsp_block.get());

  // Call StartAdvertising, with the callback
  advertiser_->StartAdvertising(address, *data_block, *scan_rsp_block,
                                std::move(adv_conn_cb), interval_ms, anonymous,
                                std::move(status_cb));
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
}  // namespace btlib
