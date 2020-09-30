// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_USB_PERIPHERAL_UTILS_EVENT_WATCHER_H_
#define LIB_USB_PERIPHERAL_UTILS_EVENT_WATCHER_H_

#include <fuchsia/hardware/usb/peripheral/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl-async/cpp/bind.h>
#include <zircon/types.h>

namespace usb_peripheral_utils {

class __EXPORT EventWatcher
    : public ::llcpp::fuchsia::hardware::usb::peripheral::Events::Interface {
 public:
  explicit EventWatcher(async::Loop* loop, zx::channel svc, size_t functions)
      : loop_(loop), functions_(functions) {
    fidl::BindSingleInFlightOnly(loop->dispatcher(), std::move(svc), this);
  }

  void FunctionRegistered(FunctionRegisteredCompleter::Sync completer);
  void FunctionsCleared(FunctionsClearedCompleter::Sync completer);

  bool all_functions_registered() { return functions_registered_ == functions_; }
  bool all_functions_cleared() { return all_functions_cleared_; }

 private:
  async::Loop* loop_;
  const size_t functions_;
  size_t functions_registered_ = 0;

  bool all_functions_cleared_ = false;
};

}  // namespace usb_peripheral_utils

#endif  // LIB_USB_PERIPHERAL_UTILS_EVENT_WATCHER_H_
