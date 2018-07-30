// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/story_puppet_master_impl.h"

#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/logging.h>

#include "peridot/bin/user_runner/puppet_master/story_command_executor.h"
#include "peridot/bin/user_runner/storage/session_storage.h"

namespace modular {

namespace {

class ExecuteOperation : public Operation<fuchsia::modular::ExecuteResult> {
 public:
  ExecuteOperation(SessionStorage* const session_storage,
                   StoryCommandExecutor* const executor,
                   fidl::StringPtr story_name,
                   std::vector<fuchsia::modular::StoryCommand> commands,
                   ResultCall done)
      : Operation("StoryPuppetMasterImpl.ExecuteOpreation", std::move(done)),
        session_storage_(session_storage),
        executor_(executor),
        story_name_(std::move(story_name)),
        commands_(std::move(commands)) {}

 private:
  void Run() override {
    session_storage_->GetStoryDataByName(story_name_)
        ->WeakThen(GetWeakPtr(),
                   [this](fuchsia::modular::internal::StoryDataPtr data) {
                     if (data) {
                       story_id_ = data->story_info.id;
                       ExecuteCommands();
                       return;
                     }

                     CreateStory();
                   });
  }

  void CreateStory() {
    session_storage_
        ->CreateStory(story_name_, nullptr /* extra_info */,
                      {} /* story_options */)
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
  fidl::StringPtr story_name_;
  std::vector<fuchsia::modular::StoryCommand> commands_;

  fidl::StringPtr story_id_;
};

}  // namespace

StoryPuppetMasterImpl::StoryPuppetMasterImpl(
    fidl::StringPtr story_name, SessionStorage* const session_storage,
    StoryCommandExecutor* const executor)
    : story_name_(story_name),
      session_storage_(session_storage),
      executor_(executor) {
  FXL_DCHECK(session_storage != nullptr);
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
  // First ensure that the story is created.
  operations_.Add(new ExecuteOperation(session_storage_, executor_, story_name_,
                                       std::move(enqueued_commands_), done));
}

}  // namespace modular
