// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <algorithm>
#include <string>
#include <vector>

#include <src/modular/bin/basemgr/child_listener.h>

namespace modular {

ChildListener::ChildListener(sys::ServiceDirectory* svc, async_dispatcher_t* dispatcher,
                             const std::vector<cpp17::string_view>& child_names)
    : svc_(svc), dispatcher_(dispatcher), weak_factory_(this) {
  for (const auto& name : child_names) {
    std::string path = "fuchsia.component.Binder." + std::string(name);
    impls_.emplace_back(std::make_unique<ChildListenerImpl>(/*name=*/std::string(name), path));
  }
}

void ChildListener::StartListening(fuchsia::hardware::power::statecontrol::Admin* administrator) {
  for (auto& impl : impls_) {
    ConnectToChild(0, impl.get(), administrator);
  }
}

void ChildListener::ConnectToChild(size_t attempt, ChildListenerImpl* impl,
                                   fuchsia::hardware::power::statecontrol::Admin* administrator) {
  if (attempt == kMaxCrashRecoveryLimit) {
    FX_LOGS(ERROR) << "Failed to connect to " << impl->GetPath() << " after "
                   << kMaxCrashRecoveryLimit << " attempts. Rebooting system.";
    administrator->Reboot(fuchsia::hardware::power::statecontrol::RebootReason::SESSION_FAILURE,
                          [](fuchsia::hardware::power::statecontrol::Admin_Reboot_Result status) {
                            if (status.is_err()) {
                              FX_PLOGS(FATAL, status.err()) << "Failed to reboot";
                            }
                          });
    return;
  }

  FX_LOGS(INFO) << "Starting child " << impl->GetName() << ". Attempt #"
                << attempt + 1;  // Add 1 since attempt is 0-based.

  auto connection_task = impl->Connect(
      svc_, dispatcher_,
      /*on_error=*/
      [weak_this = weak_factory_.GetWeakPtr(), impl, attempt, administrator](zx_status_t status) {
        FX_LOGS(WARNING) << "Lost connection to child " << impl->GetName() << ": "
                         << zx_status_get_string(status);
        weak_this->ConnectToChild(attempt + 1, impl, administrator);
      });

  async::PostTask(dispatcher_, std::move(connection_task));
}

ChildListener::ChildListenerImpl::ChildListenerImpl(std::string name, std::string path)
    : name_(std::move(name)), path_(std::move(path)), weak_factory_(this) {}

fit::closure ChildListener::ChildListenerImpl::Connect(sys::ServiceDirectory* svc,
                                                       async_dispatcher_t* dispatcher,
                                                       fit::function<void(zx_status_t)> on_error) {
  return [=, on_error = std::move(on_error), weak_this = weak_factory_.GetWeakPtr()]() mutable {
    zx_status_t status =
        svc->Connect(weak_this->binder_.NewRequest(dispatcher), weak_this->GetPath());
    if (status == ZX_OK) {
      weak_this->binder_.set_error_handler(std::move(on_error));
    } else {
      on_error(status);
    }
  };
}

const std::string& ChildListener::ChildListenerImpl::GetName() const { return name_; }

const std::string& ChildListener::ChildListenerImpl::GetPath() const { return path_; }

}  // namespace modular
