// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_USB_PERIPHERAL_UTILS_EVENT_WATCHER_H_
#define LIB_USB_PERIPHERAL_UTILS_EVENT_WATCHER_H_

#include <fidl/fuchsia.hardware.usb.peripheral/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl-async/cpp/bind.h>
#include <zircon/types.h>

namespace usb_peripheral_utils {

class __EXPORT EventWatcher : public fidl::WireServer<fuchsia_hardware_usb_peripheral::Events> {
 public:
  explicit EventWatcher(async::Loop* loop, zx::channel svc, size_t functions)
      : loop_(loop), functions_(functions) {
    fidl::BindSingleInFlightOnly(loop->dispatcher(), std::move(svc), this);
  }

  void FunctionRegistered(FunctionRegisteredRequestView request,
                          FunctionRegisteredCompleter::Sync& completer) override;
  void FunctionsCleared(FunctionsClearedRequestView request,
                        FunctionsClearedCompleter::Sync& completer) override;

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
