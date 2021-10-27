// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/system_log_recorder/pressure_watcher.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

namespace forensics::feedback_data::system_log_recorder {
namespace {

// Executes a callback each time fuchshia.memorypressure.Watcher/OnLevelChanged is called.
class CallbackPressureWatcher : public fuchsia::memorypressure::Watcher {
 public:
  CallbackPressureWatcher(::fit::function<void(fuchsia::memorypressure::Level)> on_level_changed)
      : on_level_changed_(std::move(on_level_changed)) {}

  void OnLevelChanged(const fuchsia::memorypressure::Level level,
                      OnLevelChangedCallback callback) override {
    callback();
    on_level_changed_(level);
  }

 private:
  ::fit::function<void(fuchsia::memorypressure::Level)> on_level_changed_;
};

}  // namespace

PressureWatcher::PressureWatcher(async_dispatcher_t* dispatcher,
                                 std::shared_ptr<sys::ServiceDirectory> services,
                                 OnLevelChangedFn on_level_changed)
    : dispatcher_(dispatcher),
      services_(services),
      watcher_(std::make_unique<CallbackPressureWatcher>(std::move(on_level_changed))),
      connection_(watcher_.get()) {
  Connect();
  connection_.set_error_handler(::fit::bind_member(this, &PressureWatcher::OnError));
}

void PressureWatcher::Connect() {
  // Fire-and-forget the request to register the watcher with the memory pressure signal source.
  fuchsia::memorypressure::ProviderPtr provider;
  services_->Connect(provider.NewRequest(dispatcher_));
  provider->RegisterWatcher(connection_.NewBinding(dispatcher_));
}

void PressureWatcher::OnError(const zx_status_t status) {
  FX_PLOGS(WARNING, status) << "Lost connection to client of fuchsia.memorypressure.Watcher";
}

}  // namespace forensics::feedback_data::system_log_recorder
