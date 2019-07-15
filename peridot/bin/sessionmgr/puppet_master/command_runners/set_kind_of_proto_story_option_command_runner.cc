// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/puppet_master/command_runners/set_kind_of_proto_story_option_command_runner.h"

namespace modular {

class SetKindOfProtoStoryOptionCall : public Operation<fuchsia::modular::ExecuteResult> {
 public:
  SetKindOfProtoStoryOptionCall(SessionStorage* const session_storage, fidl::StringPtr story_id,
                                bool value, ResultCall done)
      : Operation("SetKindOfProtoStoryOption::SetKindOfProtoStoryOptionCall", std::move(done)),
        session_storage_(session_storage),
        story_id_(story_id),
        value_(value) {}

 private:
  void Run() override {
    FlowToken flow{this, &result_};
    result_.status = fuchsia::modular::ExecuteStatus::OK;
    session_storage_->GetStoryData(story_id_)->Then(
        [this, flow](fuchsia::modular::internal::StoryDataPtr data) {
          if (data->story_options().kind_of_proto_story == value_) {
            // Finish early since there's nothing to update.
            return;
          }
          data->story_options().Clone(&options_);
          UpdateOptions(flow);
        });
  }

  void UpdateOptions(FlowToken flow) {
    options_.kind_of_proto_story = value_;
    session_storage_->UpdateStoryOptions(story_id_, std::move(options_))->Then([flow]() {
    });  // Operation finishes when flow goes out of scope.
  }

  SessionStorage* const session_storage_;
  fidl::StringPtr story_id_;
  bool value_;
  fuchsia::modular::StoryOptions options_;
  fuchsia::modular::ExecuteResult result_;
};

SetKindOfProtoStoryOptionCommandRunner::SetKindOfProtoStoryOptionCommandRunner(
    SessionStorage* const session_storage)
    : session_storage_(session_storage) {
  FXL_DCHECK(session_storage_);
}

SetKindOfProtoStoryOptionCommandRunner::~SetKindOfProtoStoryOptionCommandRunner() = default;

void SetKindOfProtoStoryOptionCommandRunner::Execute(
    fidl::StringPtr story_id, StoryStorage* const story_storage,
    fuchsia::modular::StoryCommand command,
    fit::function<void(fuchsia::modular::ExecuteResult)> done) {
  FXL_CHECK(command.is_set_kind_of_proto_story_option());

  operation_queue_.Add(std::make_unique<SetKindOfProtoStoryOptionCall>(
      session_storage_, story_id, command.set_kind_of_proto_story_option().value, std::move(done)));
}

}  // namespace modular
