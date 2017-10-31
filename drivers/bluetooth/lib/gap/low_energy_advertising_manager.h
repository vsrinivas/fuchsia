// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/gap/advertising_data.h"
#include "garnet/drivers/bluetooth/lib/gap/gap.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_advertiser.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_connection_manager.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"
#include "lib/fsl/tasks/message_loop.h"

namespace bluetooth {

namespace hci {
class Transport;
}  // namespace hci

namespace gap {

namespace internal {
class ActiveAdvertisement;
}  // namespace internal

class LowEnergyAdvertisingManager {
 public:
  // Build an AdvertisingManaer which will use the underlying advertiser
  // |advertiser| to make advertisements.
  // |connection_manager| and |device_cache| are expected to outlive this
  // object.
  explicit LowEnergyAdvertisingManager(
      std::unique_ptr<LowEnergyAdvertiser> advertiser);

  virtual ~LowEnergyAdvertisingManager();

  // Asynchronously attempts to start advertising a set of |data| with
  // additional scan response data |scan_rsp|.
  // If |connect_callback| is provided, the advertisement will be connectable
  // and it will be called with the returned advertisement_id and a pointer to
  // the new connection, at which point the advertisement will have been
  // stopped.
  //
  // Returns false if the parameters represent an invalid advertisement:
  //  * if |anonymous| is true but |callback| is set
  //
  // result_callback is called:
  //  - with an |advertisement_id| which can be used to stop advertising
  //    or disambiguate calls to |callback|, and an empty |error|
  //  - with an empty |advertisement_id| and one of these statuses:
  //    * hci::kConnectionLimitExceeded if another set cannot be advertised
  //    * hci::kMemoryCapacityExceeded if the |data| is too large
  //    * the actual hci error reported from the controller, otherwise.
  // TODO(jamuraa): Introduce stack error codes that are separate from HCI error
  // codes.
  using ConnectionCallback = std::function<void(std::string advertisement_id,
                                                LowEnergyConnectionRefPtr)>;
  using AdvertisingResultCallback =
      std::function<void(std::string advertisement_id, hci::Status status)>;
  bool StartAdvertising(const AdvertisingData& data,
                        const AdvertisingData& scan_rsp,
                        const ConnectionCallback& connect_callback,
                        uint32_t interval_ms,
                        bool anonymous,
                        const AdvertisingResultCallback& result_callback);

  // Stop advertising the advertisement with the id |advertisement_id|
  // Returns true if an advertisement was stopped, and false otherwise.
  // This function is idempotent.
  bool StopAdvertising(std::string advertisement_id);

 private:
  // Active advertisements, indexed by id.
  std::unordered_map<std::string,
                     std::unique_ptr<internal::ActiveAdvertisement>>
      advertisements_;

  // The task loop that the advertisement requests will run on.
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  // The instantiateed advertiser used to communicate with the adapter.
  std::unique_ptr<LowEnergyAdvertiser> advertiser_;

  // Note: Should remain the last member so it'll be destroyed and
  // invalidate it's pointers before other members are destroyed.
  fxl::WeakPtrFactory<LowEnergyAdvertisingManager> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyAdvertisingManager);
};

}  // namespace gap

}  // namespace bluetooth
