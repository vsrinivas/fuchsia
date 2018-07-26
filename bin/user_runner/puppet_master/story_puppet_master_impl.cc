// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/story_puppet_master_impl.h"

#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/logging.h>

#include "peridot/bin/user_runner/puppet_master/story_command_executor.h"

namespace modular {

StoryPuppetMasterImpl::StoryPuppetMasterImpl(
    fidl::StringPtr story_id, StoryCommandExecutor* const executor)
    : story_id_(story_id), executor_(executor), weak_factory_(this) {
  FXL_DCHECK(executor != nullptr);
}

StoryPuppetMasterImpl::~StoryPuppetMasterImpl() = default;

void StoryPuppetMasterImpl::Enqueue(
    fidl::VectorPtr<fuchsia::modular::StoryCommand> commands) {
  if (!commands) {
    return;
  }
  enqueued_commands_.insert(enqueued_commands_.end(),
                            make_move_iterator(commands->begin()),
                            make_move_iterator(commands->end()));
}

void StoryPuppetMasterImpl::Execute(ExecuteCallback done) {
  executor_->ExecuteCommands(
      story_id_, std::move(enqueued_commands_),
      [weak_ptr = weak_factory_.GetWeakPtr(),
       done = std::move(done)](fuchsia::modular::ExecuteResult result) {
        // If the StoryPuppetMasterImpl is gone, the connection that would
        // handle |done| is also gone.
        if (!weak_ptr) {
          return;
        }
        // Adopt the story id from the StoryCommandExecutor.
        if (weak_ptr->story_id_) {
          FXL_DCHECK(result.story_id == weak_ptr->story_id_);
        }
        weak_ptr->story_id_ = result.story_id;
        done(std::move(result));
      });
}

}  // namespace modular
