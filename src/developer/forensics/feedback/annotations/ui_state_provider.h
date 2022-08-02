// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_UI_STATE_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_UI_STATE_PROVIDER_H_

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <set>
#include <string>

#include "src/developer/forensics/feedback/annotations/provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics::feedback {

// Caches the UI activity state and dynamically computes the duration since the last state change
class UIStateProvider : public fuchsia::ui::activity::Listener,
                        public CachedAsyncAnnotationProvider,
                        public DynamicSyncAnnotationProvider {
 public:
  explicit UIStateProvider(std::unique_ptr<timekeeper::Clock> clock);

  std::set<std::string> GetKeys() const override;

  // Returns the duration since the last state change
  Annotations Get() override;

  // Sets the most recent UI activity state with |callback|
  void GetOnUpdate(::fit::function<void(Annotations)> callback) override;
  void OnStateChanged(fuchsia::ui::activity::State state, int64_t transition_time,
                      OnStateChangedCallback callback) override;

 private:
  std::optional<std::string> current_state_;
  std::optional<zx::time> last_transition_time_;

  ::fit::function<void(Annotations)> on_update_;

  std::unique_ptr<timekeeper::Clock> clock_;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_UI_STATE_PROVIDER_H_
