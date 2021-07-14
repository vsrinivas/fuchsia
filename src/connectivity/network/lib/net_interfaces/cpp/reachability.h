// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_LIB_NET_INTERFACES_CPP_REACHABILITY_H_
#define SRC_CONNECTIVITY_NETWORK_LIB_NET_INTERFACES_CPP_REACHABILITY_H_

#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/fpromise/result.h>

#include <string>

#include "net_interfaces.h"

namespace net::interfaces {

// Reads events from |watcher| and calls |callback| with a boolean network-reachability value iff it
// has changed since the previously-reported value (the first value reported will be based on the
// first interface existing or added event).
//
// Upon encountering a non-recoverable error, e.g. the watcher FIDL
// channel closing or receiving an invalid event from the server, |callback| will be called with
// the error, and will never be called again.
class ReachabilityWatcher final {
 public:
  enum class Error {
    kChannelClosed,
  };

  using ErrorVariant = std::variant<PropertiesMap::UpdateErrorVariant, Error>;

  ReachabilityWatcher(fuchsia::net::interfaces::WatcherPtr watcher,
                      ::fit::function<void(fpromise::result<bool, ErrorVariant>)> callback);

  static std::string error_get_string(ErrorVariant variant);

 private:
  void HandleEvent(fuchsia::net::interfaces::Event event);

  fuchsia::net::interfaces::WatcherPtr watcher_;
  ::fit::function<void(fpromise::result<bool, ErrorVariant>)> callback_;

  net::interfaces::PropertiesMap interface_properties_;
  std::optional<bool> reachable_;

  // Helper type for visitor in |reachability_watcher_error_get_string|.
  template <class... Ts>
  struct ErrorVisitor : Ts... {
    using Ts::operator()...;
  };
  template <class... Ts>
  ErrorVisitor(Ts...) -> ErrorVisitor<Ts...>;
};

}  // namespace net::interfaces

#endif  // SRC_CONNECTIVITY_NETWORK_LIB_NET_INTERFACES_CPP_REACHABILITY_H_
