// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver/component/cpp/promise.h>
#include <lib/fpromise/bridge.h>

namespace driver {

namespace internal {
fpromise::result<fidl::WireSharedClient<fuchsia_io::File>, zx_status_t> OpenWithResult(
    const driver::Namespace& ns, async_dispatcher_t* dispatcher, const char* path,
    fuchsia_io::wire::OpenFlags flags) {
  auto file = ns.Open<fuchsia_io::File>(path, flags);
  if (file.is_error()) {
    return fpromise::error(file.status_value());
  }
  fidl::WireSharedClient client(std::move(*file), dispatcher);
  return fpromise::ok(std::move(client));
}
}  // namespace internal

fpromise::promise<void, fuchsia_driver_framework::wire::NodeError> AddChild(
    fidl::WireSharedClient<fuchsia_driver_framework::Node>& client,
    fuchsia_driver_framework::wire::NodeAddArgs args,
    fidl::ServerEnd<fuchsia_driver_framework::NodeController> controller,
    fidl::ServerEnd<fuchsia_driver_framework::Node> node) {
  fpromise::bridge<void, fuchsia_driver_framework::wire::NodeError> bridge;
  auto callback =
      [completer = std::move(bridge.completer)](
          fidl::WireUnownedResult<fuchsia_driver_framework::Node::AddChild>& result) mutable {
        if (!result.ok()) {
          completer.complete_error(fuchsia_driver_framework::wire::NodeError::kInternal);
          return;
        }
        if (result->is_error()) {
          completer.complete_error(result->error_value());
          return;
        }
        completer.complete_ok();
      };
  client->AddChild(args, std::move(controller), std::move(node))
      .ThenExactlyOnce(std::move(callback));
  return bridge.consumer.promise();
}

}  // namespace driver
