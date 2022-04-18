// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "promise.h"

#include <lib/fpromise/bridge.h>

namespace fdf = fuchsia_driver_framework;

namespace driver {

fpromise::promise<void, fdf::wire::NodeError> AddChild(
    fidl::WireSharedClient<fdf::Node>& client, fdf::wire::NodeAddArgs args,
    fidl::ServerEnd<fdf::NodeController> controller, fidl::ServerEnd<fdf::Node> node) {
  fpromise::bridge<void, fdf::wire::NodeError> bridge;
  auto callback = [completer = std::move(bridge.completer)](
                      fidl::WireUnownedResult<fdf::Node::AddChild>& result) mutable {
    if (!result.ok()) {
      completer.complete_error(fdf::wire::NodeError::kInternal);
      return;
    }
    if (result->result.is_err()) {
      completer.complete_error(result->result.err());
      return;
    }
    completer.complete_ok();
  };
  client->AddChild(args, std::move(controller), std::move(node))
      .ThenExactlyOnce(std::move(callback));
  return bridge.consumer.promise();
}

}  // namespace driver
