// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <memory>

#include <lib/fit/function.h>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"

namespace btlib {
namespace hci {

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
  //
  // If |address| is currently advertised, the advertisement is updated.
  //
  // If |connect_callback| is provided, the advertisement will be connectable,
  // and the provided callback will be called with a connection reference
  // when this advertisement is connected to and the advertisement has been
  // stopped.
  //
  // Provides results in |callback|. If advertising is setup, the final
  // interval of advertising is provided in |interval_ms| and |status|
  // is kSuccess.
  //
  // Otherwise, |status| will indicate the type of error.
  //
  // |callback| may be called before this function returns, but will
  // be called before any calls to |connect_callback|.
  using AdvertisingStatusCallback =
      fit::function<void(uint32_t interval_ms, Status status)>;
  using ConnectionCallback = std::function<void(ConnectionPtr link)>;
  virtual void StartAdvertising(const common::DeviceAddress& address,
                                const common::ByteBuffer& data,
                                const common::ByteBuffer& scan_rsp,
                                const ConnectionCallback& connect_callback,
                                uint32_t interval_ms,
                                bool anonymous,
                                AdvertisingStatusCallback callback) = 0;

  // Stops any advertisement currently active on |address|. Idempotent and
  // asynchronous. Returns true if advertising will be stopped, false otherwise.
  virtual bool StopAdvertising(const common::DeviceAddress& address) = 0;

  // Callback for an incoming connection.  |connection| should be an LE
  // connection that has been remotely-intiated.  This function should be called
  // in reaction to any connection that was not initiated locally.   This object
  // will determine if it was a result of an active advertisement and call the
  // appropriate callback.
  virtual void OnIncomingConnection(ConnectionPtr connection) = 0;
};

}  // namespace hci
}  // namespace btlib
