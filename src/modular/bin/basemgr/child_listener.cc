// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <fuchsia/session/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <src/modular/bin/basemgr/child_listener.h>

namespace modular {

namespace {
std::string SessionRestartErrorToString(fuchsia::session::RestartError err) {
  switch (err) {
    case fuchsia::session::RestartError::CREATE_COMPONENT_FAILED:
      return "CREATE_COMPONENT_FAILED";
    case fuchsia::session::RestartError::DESTROY_COMPONENT_FAILED:
      return "DESTROY_COMPONENT_FAILED";
    case fuchsia::session::RestartError::NOT_FOUND:
      return "NOT_FOUND";
    case fuchsia::session::RestartError::NOT_RUNNING:
      return "NOT_RUNNING";
  }

  return "UNKOWN";
}
}  // namespace

ChildListener::ChildListener(sys::ServiceDirectory* svc, async_dispatcher_t* dispatcher,
                             const std::vector<Child>& children, size_t backoff_base,
                             inspect::Node child_restart_tracker)
    : svc_(svc),
      dispatcher_(dispatcher),
      backoff_base_(backoff_base),
      child_restart_tracker_(std::move(child_restart_tracker)),
      weak_factory_(this) {
  FX_LOGS(INFO) << "Backoff Base: " << backoff_base << " minutes.";
  for (const auto& child : children) {
    std::string path = "fuchsia.component.Binder." + std::string(child.name);
    auto num_restarts = child_restart_tracker_.CreateUint(child.name, 0);
    impls_.emplace_back(std::make_unique<ChildListenerImpl>(child, path, std::move(num_restarts)));
  }
}

void ChildListener::StartListening(fuchsia::session::Restarter* session_restarter) {
  for (auto& impl : impls_) {
    if (impl->IsCritical()) {
      ConnectToCriticalChild(impl.get(), session_restarter);
    } else {
      ConnectToEagerChild(impl.get(), /*attempt=*/0);
    }
  }
}

void ChildListener::ConnectToCriticalChild(ChildListenerImpl* impl,
                                           fuchsia::session::Restarter* session_restarter) {
  FX_LOGS(INFO) << "Starting critical child " << impl->GetName() << ".";

  auto connection_task = impl->Connect(
      svc_, dispatcher_,
      /*on_error=*/
      [weak_this = weak_factory_.GetWeakPtr(), impl, session_restarter](zx_status_t status) {
        FX_LOGS(WARNING) << "Lost connection to critical child " << impl->GetName() << ": "
                         << zx_status_get_string(status);
        session_restarter->Restart([](fuchsia::session::Restarter_Restart_Result status) {
          if (status.is_err()) {
            FX_LOGS(FATAL) << "Failed to restart session: "
                           << SessionRestartErrorToString(status.err());
          }
        });
        return;
      });

  async::PostTask(dispatcher_, std::move(connection_task));
}

void ChildListener::ConnectToEagerChild(ChildListenerImpl* impl, size_t attempt) {
  if (attempt == kMaxCrashRecoveryLimit) {
    impl->IncrementRestartCount();
    FX_LOGS(INFO) << "Failed to connect to " << impl->GetPath() << " after "
                  << kMaxCrashRecoveryLimit << " attempts. No further attempts will be made.";
    return;
  }

  FX_LOGS(INFO) << "Starting eager child " << impl->GetName() << ". Attempt #"
                << attempt + 1;  // Add 1 since attempt is 0-based.

  auto connection_task =
      impl->Connect(svc_, dispatcher_,
                    /*on_error=*/
                    [weak_this = weak_factory_.GetWeakPtr(), impl, attempt](zx_status_t status) {
                      FX_LOGS(WARNING) << "Lost connection to child " << impl->GetName() << ": "
                                       << zx_status_get_string(status);
                      weak_this->ConnectToEagerChild(impl, attempt + 1);
                    });

  if (attempt) {
    impl->IncrementRestartCount();
    const int64_t delay = static_cast<int64_t>(
        std::pow(static_cast<double>(backoff_base_), static_cast<double>(attempt)));
    async::PostDelayedTask(dispatcher_, std::move(connection_task), zx::min(delay));
  } else {
    async::PostTask(dispatcher_, std::move(connection_task));
  }
}

ChildListener::ChildListenerImpl::ChildListenerImpl(Child child, std::string path,
                                                    inspect::UintProperty num_restarts)
    : child_(child),
      path_(std::move(path)),
      num_restarts_(std::move(num_restarts)),
      weak_factory_(this) {}

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

void ChildListener::ChildListenerImpl::IncrementRestartCount() { num_restarts_.Add(1); }

cpp17::string_view ChildListener::ChildListenerImpl::GetName() const { return child_.name; }

const std::string& ChildListener::ChildListenerImpl::GetPath() const { return path_; }

bool ChildListener::ChildListenerImpl::IsCritical() const { return child_.critical; }

}  // namespace modular
