// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_MAC_PUBLIC_NETWORK_MAC_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_MAC_PUBLIC_NETWORK_MAC_H_

#include <memory>

#include <ddktl/protocol/network/mac.h>
#include <fbl/alloc_checker.h>

namespace network {

class MacAddrDeviceInterface {
 public:
  virtual ~MacAddrDeviceInterface() = default;
  // Creates a new MacAddrDeviceInterface that is bound to the provided parent.
  static zx_status_t Create(ddk::MacAddrImplProtocolClient parent,
                            std::unique_ptr<MacAddrDeviceInterface>* out);

  // Binds the request channel req to this MacAddrDeviceInterface. Requests will be handled on the
  // provided dispatcher.
  virtual zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel req) = 0;

  // Tears down this device, closing all bound FIDL clients.
  // It is safe to destroy this `MacAddrDeviceInterface` instance only once the callback is invoked.
  virtual void Teardown(fit::callback<void()> callback) = 0;

 protected:
  MacAddrDeviceInterface() = default;
};

}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_MAC_PUBLIC_NETWORK_MAC_H_
