// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_ACTIVITY_LISTENER_IMPL_H_
#define SRC_COBALT_BIN_APP_ACTIVITY_LISTENER_IMPL_H_

#include <functional>

#include "src/lib/fxl/macros.h"
#include "third_party/cobalt/src/public/activity_listener_interface.h"

namespace cobalt {

// TODO(fxbug.dev/113288): this is only temporary until Cobalt Core does not do any activity
// listening.
class ActivityListenerImpl : public cobalt::ActivityListenerInterface {
 public:
  ActivityListenerImpl() = default;
  ~ActivityListenerImpl() = default;

  void Start(const std::function<void(ActivityState)>& callback) override {
    callback(ActivityState::IDLE);
  };

  ActivityState state() override { return ActivityState::IDLE; }

  FXL_DISALLOW_COPY_AND_ASSIGN(ActivityListenerImpl);
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_ACTIVITY_LISTENER_IMPL_H_
