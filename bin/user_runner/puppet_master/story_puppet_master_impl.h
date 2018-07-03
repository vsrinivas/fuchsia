// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_STORY_PUPPET_MASTER_IMPL_H_
#define PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_STORY_PUPPET_MASTER_IMPL_H_

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/memory/weak_ptr.h>

namespace modular {

class StoryCommandExecutor;

// An implementation of fuchsia::modular::StoryPuppetMaster which delegates
// story command execution to a StoryCommandExecutor.
class StoryPuppetMasterImpl : public fuchsia::modular::StoryPuppetMaster {
 public:
  StoryPuppetMasterImpl(fidl::StringPtr story_id,
                        StoryCommandExecutor* executor_);
  ~StoryPuppetMasterImpl() override;

 private:
  // |fuchsia::modular::StoryPuppetMaster|
  void Enqueue(
      fidl::VectorPtr<fuchsia::modular::StoryCommand> commands) override;

  // |fuchsia::modular::StoryPuppetMaster|
  void Execute(ExecuteCallback done) override;

  fidl::StringPtr story_id_;
  StoryCommandExecutor* const executor_;  // Not owned.

  std::vector<fuchsia::modular::StoryCommand> enqueued_commands_;

  fxl::WeakPtrFactory<StoryPuppetMasterImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryPuppetMasterImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_STORY_PUPPET_MASTER_IMPL_H_
