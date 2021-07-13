// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/reboot_methods_watcher_register.h"

namespace forensics {
namespace stubs {

void RebootMethodsWatcherRegister::Register(
    ::fidl::InterfaceHandle<fuchsia::hardware::power::statecontrol::RebootMethodsWatcher> watcher) {
  watcher_ = watcher.Bind();
  watcher_->OnReboot(reason_, [] {});
}

void RebootMethodsWatcherRegisterHangs::Register(
    ::fidl::InterfaceHandle<fuchsia::hardware::power::statecontrol::RebootMethodsWatcher> watcher) {
  watcher_ = watcher.Bind();
}

}  // namespace stubs
}  // namespace forensics
