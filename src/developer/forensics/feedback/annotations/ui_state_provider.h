// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_UI_STATE_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_UI_STATE_PROVIDER_H_

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>

#include "src/developer/forensics/feedback/annotations/provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/lib/backoff/backoff.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics::feedback {

// Caches the UI activity state and dynamically computes the duration since the last state change
class UIStateProvider : public fuchsia::ui::activity::Listener,
                        public CachedAsyncAnnotationProvider,
                        public DynamicSyncAnnotationProvider {
 public:
  UIStateProvider(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                  std::unique_ptr<timekeeper::Clock> clock,
                  std::unique_ptr<backoff::Backoff> backoff);

  std::set<std::string> GetKeys() const override;

  // Returns the duration since the last state change
  Annotations Get() override;

  // Sets the most recent UI activity state with |callback|
  void GetOnUpdate(::fit::function<void(Annotations)> callback) override;
  void OnStateChanged(fuchsia::ui::activity::State state, int64_t transition_time,
                      OnStateChangedCallback callback) override;

 private:
  void OnDisconnect();
  void StartListening();

  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  std::unique_ptr<timekeeper::Clock> clock_;
  std::unique_ptr<backoff::Backoff> backoff_;

  std::optional<ErrorOr<std::string>> current_state_;
  std::variant<std::monostate, Error, zx::time> last_transition_time_;

  ::fit::function<void(Annotations)> on_update_;

  fuchsia::ui::activity::ProviderPtr provider_ptr_;
  fidl::Binding<fuchsia::ui::activity::Listener> binding_{this};
  async::TaskClosureMethod<UIStateProvider, &UIStateProvider::StartListening> reconnect_task_{this};
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_UI_STATE_PROVIDER_H_
