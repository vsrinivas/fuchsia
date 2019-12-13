// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_PUPPET_MASTER_STORY_PUPPET_MASTER_IMPL_H_
#define SRC_MODULAR_BIN_SESSIONMGR_PUPPET_MASTER_STORY_PUPPET_MASTER_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <memory>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/modular/bin/sessionmgr/puppet_master/story_command_executor.h"
#include "src/modular/lib/async/cpp/operation.h"

namespace modular {

class SessionStorage;
class StoryCommandExecutor;

// An implementation of fuchsia::modular::StoryPuppetMaster which delegates
// story command execution to a StoryCommandExecutor.
class StoryPuppetMasterImpl : public fuchsia::modular::StoryPuppetMaster {
 public:
  StoryPuppetMasterImpl(std::string story_name, OperationContainer* operations,
                        SessionStorage* session_storage, StoryCommandExecutor* executor);
  ~StoryPuppetMasterImpl() override;

  const std::string& story_name() const { return story_name_; }

 private:
  // |StoryPuppetMaster|
  void Enqueue(std::vector<fuchsia::modular::StoryCommand> commands) override;

  // |StoryPuppetMaster|
  void Execute(ExecuteCallback done) override;

  // |StoryPuppetMaster|
  void SetStoryInfoExtra(std::vector<fuchsia::modular::StoryInfoExtraEntry> story_info_extra,
                         SetStoryInfoExtraCallback callback) override;

  // |StoryPuppetMaster|
  void Annotate(std::vector<fuchsia::modular::Annotation> annotations,
                AnnotateCallback callback) override;

  // |StoryPuppetMaster|
  void AnnotateModule(std::string module_id, std::vector<fuchsia::modular::Annotation> annotations,
                      AnnotateModuleCallback callback) override;

  const std::string story_name_;
  SessionStorage* const session_storage_;  // Not owned.
  StoryCommandExecutor* const executor_;   // Not owned.

  std::vector<fuchsia::modular::StoryCommand> enqueued_commands_;

  OperationContainer* const operations_;  // Not owned.

  fxl::WeakPtrFactory<StoryPuppetMasterImpl> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryPuppetMasterImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_PUPPET_MASTER_STORY_PUPPET_MASTER_IMPL_H_
