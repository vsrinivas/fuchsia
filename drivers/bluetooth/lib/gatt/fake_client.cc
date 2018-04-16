// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_client.h"

#include "garnet/drivers/bluetooth/lib/gatt/client.h"

namespace btlib {
namespace gatt {
namespace testing {

using att::StatusCallback;

FakeClient::FakeClient(async_t* dispatcher)
    : dispatcher_(dispatcher), weak_ptr_factory_(this) {
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

void FakeClient::DiscoverCharacteristics(att::Handle range_start,
                                         att::Handle range_end,
                                         CharacteristicCallback chrc_callback,
                                         StatusCallback status_callback) {
  last_chrc_discovery_start_handle_ = range_start;
  last_chrc_discovery_end_handle_ = range_end;
  chrc_discovery_count_++;

  async::PostTask(dispatcher_, [this, chrc_callback, status_callback] {
    for (const auto& chrc : chrcs_) {
      chrc_callback(chrc);
    }
    status_callback(chrc_discovery_status_);
  });
}

void FakeClient::DiscoverDescriptors(att::Handle range_start,
                                     att::Handle range_end,
                                     DescriptorCallback desc_callback,
                                     StatusCallback status_callback) {
  FXL_NOTIMPLEMENTED();
}

void FakeClient::WriteRequest(att::Handle handle,
                              const common::ByteBuffer& value,
                              StatusCallback callback) {
  if (write_request_callback_) {
    write_request_callback_(handle, value, std::move(callback));
  }
}

}  // namespace testing
}  // namespace gatt
}  // namespace btlib
