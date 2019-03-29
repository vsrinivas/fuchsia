// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/puppet_master/story_puppet_master_impl.h"

#include <src/lib/fxl/logging.h>

#include "peridot/bin/sessionmgr/puppet_master/story_command_executor.h"
#include "peridot/bin/sessionmgr/storage/session_storage.h"

namespace modular {

namespace {

class ExecuteOperation : public Operation<fuchsia::modular::ExecuteResult> {
 public:
  ExecuteOperation(SessionStorage* const session_storage,
                   StoryCommandExecutor* const executor, std::string story_name,
                   fuchsia::modular::StoryOptions story_options,
                   std::vector<fuchsia::modular::StoryCommand> commands,
                   ResultCall done)
      : Operation("StoryPuppetMasterImpl.ExecuteOpreation", std::move(done)),
        session_storage_(session_storage),
        executor_(executor),
        story_name_(std::move(story_name)),
        story_options_(std::move(story_options)),
        commands_(std::move(commands)) {}

 private:
  void Run() override {
    session_storage_->GetStoryData(story_name_)
        ->WeakThen(GetWeakPtr(),
                   [this](fuchsia::modular::internal::StoryDataPtr data) {
                     if (data) {
                       story_id_ = data->story_info().id;
                       ExecuteCommands();
                       return;
                     }

                     CreateStory();
                   });
  }

  void CreateStory() {
    session_storage_
        ->CreateStory(story_name_, nullptr /* extra_info */,
                      std::move(story_options_))
        ->WeakThen(GetWeakPtr(),
                   [this](fidl::StringPtr story_id, auto /* ignored */) {
                     story_id_ = story_id;
                     ExecuteCommands();
                   });
  }

  void ExecuteCommands() {
    executor_->ExecuteCommands(story_id_, std::move(commands_),
                               [weak_ptr = GetWeakPtr(),
                                this](fuchsia::modular::ExecuteResult result) {
                                 Done(std::move(result));
                               });
  }

  SessionStorage* const session_storage_;
  StoryCommandExecutor* const executor_;
  std::string story_name_;
  fuchsia::modular::StoryOptions story_options_;
  std::vector<fuchsia::modular::StoryCommand> commands_;

  fidl::StringPtr story_id_;
};

}  // namespace

StoryPuppetMasterImpl::StoryPuppetMasterImpl(
    std::string story_name, OperationContainer* const operations,
    SessionStorage* const session_storage, StoryCommandExecutor* const executor)
    : story_name_(story_name),
      session_storage_(session_storage),
      executor_(executor),
      operations_(operations) {
  FXL_DCHECK(session_storage != nullptr);
  FXL_DCHECK(executor != nullptr);
}

StoryPuppetMasterImpl::~StoryPuppetMasterImpl() = default;

void StoryPuppetMasterImpl::Enqueue(
    std::vector<fuchsia::modular::StoryCommand> commands) {
  enqueued_commands_.insert(enqueued_commands_.end(),
                            make_move_iterator(commands.begin()),
                            make_move_iterator(commands.end()));
}

void StoryPuppetMasterImpl::Execute(ExecuteCallback done) {
  // First ensure that the story is created.
  operations_->Add(std::make_unique<ExecuteOperation>(
      session_storage_, executor_, story_name_, std::move(story_options_),
      std::move(enqueued_commands_), std::move(done)));
}

void StoryPuppetMasterImpl::SetCreateOptions(
    fuchsia::modular::StoryOptions story_options) {
  story_options_ = std::move(story_options);
}

}  // namespace modular
