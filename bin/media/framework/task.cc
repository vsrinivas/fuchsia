// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/task.h"

#include "apps/media/src/framework/engine.h"
#include "apps/media/src/framework/stages/stage.h"

namespace media {

Task::Task(Engine* engine,
           const ftl::Closure& function,
           std::vector<Stage*> stages)
    : engine_(engine), function_(function), stages_(std::move(stages)) {
  FTL_DCHECK(engine_);
  FTL_DCHECK(function_);
  FTL_DCHECK(!stages_.empty());

  // We pretend to require an additional stage to keep that task blocked for
  // now. |Unblock| allows the task to run.
  unacquired_stage_count_ = stages_.size() + 1;

  for (Stage* stage : stages_) {
    FTL_DCHECK(stage);
    stage->AcquireForTask(this);
  }
}

Task::~Task() {}

void Task::Unblock() {
  StageAcquired();
}

void Task::StageAcquired() {
  if (--unacquired_stage_count_ != 0) {
    return;
  }

  function_();

  for (Stage* stage : stages_) {
    FTL_DCHECK(stage);
    stage->ReleaseForTask(this);
  }

  engine_->DeleteTask(this);
}

}  // namespace media
