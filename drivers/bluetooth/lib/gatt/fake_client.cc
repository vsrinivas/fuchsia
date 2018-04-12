// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_client.h"

#include "garnet/drivers/bluetooth/lib/gatt/client.h"

namespace btlib {
namespace gatt {
namespace testing {

FakeClient::FakeClient(async_t* dispatcher)
    : dispatcher_(dispatcher),
      server_mtu_(att::kLEMinMTU),
      weak_ptr_factory_(this) {
  FXL_DCHECK(dispatcher_);
}

fxl::WeakPtr<Client> FakeClient::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void FakeClient::ExchangeMTU(MTUCallback callback) {
  auto task = [status = exchange_mtu_status_, mtu = server_mtu_,
               callback = std::move(callback)] { callback(status, mtu); };
  async::PostTask(dispatcher_, std::move(task));
}

void FakeClient::DiscoverPrimaryServices(ServiceCallback svc_callback,
                                         StatusCallback status_callback) {
  async::PostTask(dispatcher_, [this, svc_callback, status_callback] {
    for (const auto& svc : services_) {
      svc_callback(svc);
    }
    status_callback(service_discovery_status_);
  });
}

}  // namespace testing
}  // namespace gatt
}  // namespace btlib
