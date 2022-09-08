// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER2_RUNTIME_H_
#define LIB_DRIVER2_RUNTIME_H_

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>

namespace driver {

// DEPRECATED. Use |driver::Connect| in service_client.h instead.
//
// Connects to the runtime protocol 'protocol_name` using `client`
// and returns a fpromise::promise containing the runtime channel on success.
template <typename T>
fpromise::promise<fdf::Channel, zx_status_t> ConnectToRuntimeProtocol(
    fidl::WireSharedClient<fuchsia_driver_framework::RuntimeConnector>& client,
    std::string_view protocol_name = fidl::DiscoverableProtocolName<T>) {
  if (!client.is_valid()) {
    return fpromise::make_result_promise<fdf::Channel, zx_status_t>(
        fpromise::error(ZX_ERR_INVALID_ARGS));
  }
  auto channels = fdf::ChannelPair::Create(0);
  if (channels.is_error()) {
    return fpromise::make_result_promise<fdf::Channel, zx_status_t>(
        fpromise::error(channels.status_value()));
  }

  fpromise::bridge<fdf::Channel, zx_status_t> bridge;
  auto callback = [completer = std::move(bridge.completer), client = std::move(channels->end0)](
                      fidl::WireUnownedResult<fuchsia_driver_framework::RuntimeConnector::Connect>&
                          result) mutable {
    if (!result.ok()) {
      completer.complete_error(ZX_ERR_INTERNAL);
      return;
    }

    if (result->is_error()) {
      completer.complete_error(result->error_value());
      return;
    }
    completer.complete_ok(std::move(client));
  };

  client
      ->Connect(fidl::StringView::FromExternal(protocol_name),
                fuchsia_driver_framework::wire::RuntimeProtocolServerEnd{channels->end1.release()})
      .Then(std::move(callback));

  return bridge.consumer.promise_or(fpromise::error(ZX_ERR_CANCELED));
}

}  // namespace driver

#endif  // LIB_DRIVER2_RUNTIME_H_
