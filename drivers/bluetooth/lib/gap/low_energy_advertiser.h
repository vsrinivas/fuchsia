// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/gap/advertising_data.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"

namespace bluetooth {
namespace gap {

// TODO(jamuraa): consolidate these into a common header
class LowEnergyConnectionRef;
using LowEnergyConnectionRefPtr = std::unique_ptr<LowEnergyConnectionRef>;

class LowEnergyAdvertiser {
 public:
  virtual ~LowEnergyAdvertiser() = default;

  // Get the current limit in bytes of the advertisement data supported.
  virtual size_t GetSizeLimit() = 0;

  // Get the current limit of number of advertising sets supported, including
  // currently enabled ones.  This can change when the state of advertising
  // changes.  It should be checked before trying to add an advertisement.
  virtual size_t GetMaxAdvertisements() const = 0;

  // Attempt to start advertising |data| with scan response |scan_rsp| using
  // advertising address |address|.  If |anonymous| is set, |address| is
  // ignored.
  // If |address| is currently advertised, the advertisement is updated.
  // If |connect_callback| is provided, the advertisement will be connectable,
  // and the provided callback will be called with a connection reference
  // when this advertisement is connected to and the advertisement has been
  // stopped. Returns true if the advertisement is being processed, and false
  // otherwise.
  //
  // Calls |callback| when the advertisement setup is complete with
  // the actual interval used and status of the advertising command.
  // |callback| will be called before any calls to |connect_callback|.
  using AdvertisingResultCallback =
      std::function<void(uint32_t interval_ms, hci::Status status)>;
  using ConnectionCallback = std::function<void(LowEnergyConnectionRefPtr)>;
  virtual bool StartAdvertising(const bluetooth::common::DeviceAddress& address,
                                const AdvertisingData& data,
                                const AdvertisingData& scan_rsp,
                                const ConnectionCallback& connect_callback,
                                uint32_t interval_ms,
                                bool anonymous,
                                const AdvertisingResultCallback& callback) = 0;

  // Stops any advertisement currently active on |address|. Idempotent.
  virtual void StopAdvertising(
      const bluetooth::common::DeviceAddress& address) = 0;

  // Callback for an incoming connection.  |connection| should be an LE
  // connection that has been remotely-intiated.  This function should be called
  // in reaction to any connection that was not initiated locally.   This object
  // will determine if it was a result of an active advertisement and call the
  // appropriate callback.
  virtual void OnIncomingConnection(LowEnergyConnectionRefPtr connection) = 0;
};

}  // namespace gap
}  // namespace bluetooth
