// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_ONGOING_ACTIVITY_IMPL_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_ONGOING_ACTIVITY_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/macros.h>

namespace modular {

class OngoingActivityImpl : public fuchsia::modular::OngoingActivity {
 public:
  OngoingActivityImpl(
      fuchsia::modular::OngoingActivityType ongoing_activity_type,
      fit::closure on_destroy);

  ~OngoingActivityImpl() override;

  fuchsia::modular::OngoingActivityType GetType();

 private:
  const fuchsia::modular::OngoingActivityType ongoing_activity_type_;
  fit::closure on_destroy_;

  FXL_DISALLOW_COPY_AND_ASSIGN(OngoingActivityImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_ONGOING_ACTIVITY_IMPL_H_
