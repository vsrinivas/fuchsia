// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_STORY_PUPPET_MASTER_IMPL_H_
#define PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_STORY_PUPPET_MASTER_IMPL_H_

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>
#include <lib/fidl/cpp/binding_set.h>
#include <src/lib/fxl/memory/weak_ptr.h>

#include "peridot/bin/sessionmgr/puppet_master/story_command_executor.h"

namespace modular {

class SessionStorage;
class StoryCommandExecutor;

// An implementation of fuchsia::modular::StoryPuppetMaster which delegates
// story command execution to a StoryCommandExecutor.
class StoryPuppetMasterImpl : public fuchsia::modular::StoryPuppetMaster {
 public:
  StoryPuppetMasterImpl(std::string story_name, OperationContainer* operations,
                        SessionStorage* session_storage,
                        StoryCommandExecutor* executor);
  ~StoryPuppetMasterImpl() override;

 private:
  // |StoryPuppetMaster|
  void Enqueue(std::vector<fuchsia::modular::StoryCommand> commands) override;

  // |StoryPuppetMaster|
  void Execute(ExecuteCallback done) override;

  // |StoryPuppetMaster|
  void SetCreateOptions(fuchsia::modular::StoryOptions story_options) override;

  // |StoryPuppetMaster|
  void SetStoryInfoExtra(
      std::vector<fuchsia::modular::StoryInfoExtraEntry> story_info_extra,
      SetStoryInfoExtraCallback callback) override;

  std::string story_name_;
  SessionStorage* const session_storage_;  // Not owned.
  StoryCommandExecutor* const executor_;   // Not owned.

  std::vector<fuchsia::modular::StoryCommand> enqueued_commands_;

  OperationContainer* const operations_;  // Not owned.

  // Story options passed to |session_storage_.CreateStory|, set
  // by |SetCreateOptions|. This value is reset after the story is created
  // in the first call to |Execute|, and subsequent values are ignored.
  fuchsia::modular::StoryOptions story_options_;

  // StoryInfo extra entries passed to |session_storage_.CreateStory|, set
  // by |SetStoryInfoExtra|. This value is reset after the story is created
  // in the first call to |Execute|, and subsequent values are ignored.
  fidl::VectorPtr<fuchsia::modular::StoryInfoExtraEntry> story_info_extra_;

  fxl::WeakPtrFactory<StoryPuppetMasterImpl> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryPuppetMasterImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_PUPPET_MASTER_STORY_PUPPET_MASTER_IMPL_H_
