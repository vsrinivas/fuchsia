// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "reachability.h"

#include <lib/fit/function.h>
#include <lib/fpromise/result.h>

#include <string>

#include "src/lib/fxl/strings/string_printf.h"

namespace net::interfaces {

// static
std::string ReachabilityWatcher::error_get_string(ErrorVariant variant) {
  ErrorVisitor visitor{
      [](PropertiesMap::UpdateErrorVariant err) {
        return fxl::StringPrintf("Error updating network interface state with event: %s",
                                 PropertiesMap::update_error_get_string(err).c_str());
      },
      [](Error err) {
        switch (err) {
          case Error::kChannelClosed:
            return std::string("Unexpectedly lost connection to fuchsia.net.interfaces/Watcher");
        }
      },
  };
  return std::visit(visitor, variant);
}

ReachabilityWatcher::ReachabilityWatcher(
    fuchsia::net::interfaces::WatcherPtr watcher,
    fit::function<void(fpromise::result<bool, ReachabilityWatcher::ErrorVariant>)> callback)
    : watcher_(std::move(watcher)), callback_(std::move(callback)) {
  watcher_.set_error_handler([this](const zx_status_t status) {
    callback_(fpromise::error(ReachabilityWatcher::Error::kChannelClosed));
  });

  watcher_->Watch(fit::bind_member(this, &ReachabilityWatcher::HandleEvent));
}

void ReachabilityWatcher::HandleEvent(fuchsia::net::interfaces::Event event) {
  auto update_result = interface_properties_.Update(std::move(event));
  if (update_result.is_error()) {
    return callback_(fpromise::error(update_result.error()));
  }

  watcher_->Watch(fit::bind_member(this, &ReachabilityWatcher::HandleEvent));
  bool reachable = std::any_of(interface_properties_.properties_map().cbegin(),
                               interface_properties_.properties_map().cend(),
                               [](const auto& it) { return it.second.IsGloballyRoutable(); });
  if (!reachable_.has_value() || (reachable_.value() != reachable)) {
    reachable_ = reachable;
    callback_(fpromise::ok(reachable));
  }
}

}  // namespace net::interfaces
