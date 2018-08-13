// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-handler.h"

namespace xdc {

// static
std::unique_ptr<UsbHandler> UsbHandler::Create() {
    auto usb_handler = std::make_unique<UsbHandler>(ConstructorTag{});
    if (!usb_handler) {
        return nullptr;
    }
    // TODO(jocelyndang): need to initialize initial libusb fds.
    return usb_handler;
}

void UsbHandler::GetFdUpdates(std::map<int, short>& added_fds, std::set<int>& removed_fds) {
    // TODO(jocelyndang): implement this.
}

bool UsbHandler::HandleEvents() {
    // TODO(jocelyndang): implement this.
    return false;
}

}  // namespace xdc