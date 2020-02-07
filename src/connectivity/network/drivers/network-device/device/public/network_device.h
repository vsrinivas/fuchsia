// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_PUBLIC_NETWORK_DEVICE_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_PUBLIC_NETWORK_DEVICE_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include <memory>

#include <ddktl/protocol/network/device.h>
#include <fbl/alloc_checker.h>

namespace network {

class NetworkDevice {
 public:
  virtual ~NetworkDevice() = default;
  // Creates a new NetworkDevice that will bind to the provided parent.
  // The dispatcher is only used for slow path operations, NetworkDevice will create and manage its
  // own threads for fast path operations.
  // The parent_name argument is only used for diagnostic purposes.
  // Upon successful creation the new NetworkDevice is stored in out.
  static zx_status_t Create(async_dispatcher_t* dispatcher,
                            ddk::NetworkDeviceImplProtocolClient parent, const char* parent_name,
                            std::unique_ptr<NetworkDevice>* out);

  // Tears down the NetworkDevice.
  // A NetworkDevice must not be destroyed until the callback provided to teardown is triggered,
  // doing so may cause an assertion error. Immediately destroying a NetworkDevice that never
  // succeeded Init is allowed.
  virtual void Teardown(fit::callback<void()>) = 0;

  // Binds the request channel req to this NetworkDevice. Requests will be handled on the dispatcher
  // given to the device on creation.
  virtual zx_status_t Bind(zx::channel req) = 0;

 protected:
  NetworkDevice() = default;
};

}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_PUBLIC_NETWORK_DEVICE_H_
