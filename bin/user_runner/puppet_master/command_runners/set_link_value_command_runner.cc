// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/fsl/vmo/strings.h>

#include "peridot/bin/user_runner/puppet_master/command_runners/set_link_value_command_runner.h"
#include "peridot/bin/user_runner/storage/story_storage.h"

namespace modular {

namespace {

class SetLinkValueCall : public Operation<fuchsia::modular::ExecuteResult> {
 public:
  SetLinkValueCall(SessionStorage* const session_storage,
                   fidl::StringPtr story_id,
                   fuchsia::modular::SetLinkValue command, ResultCall done)
      : Operation("SetLinkValueCommandRunner::SetLinkValueCall",
                  std::move(done)),
        session_storage_(session_storage),
        story_id_(std::move(story_id)),
        command_(std::move(command)) {}

 private:
  void Run() override {
    FlowToken flow{this, &result_};
    result_.story_id = story_id_;
    session_storage_->GetStoryStorage(story_id_)->Then(
        [this, flow](std::unique_ptr<StoryStorage> story_storage) {
          if (!story_storage) {
            result_.status = fuchsia::modular::ExecuteStatus::INVALID_STORY_ID;
            result_.error_message = "Invalid story.";
            Done(std::move(result_));
            return;
          }
          story_storage_ = std::move(story_storage);
          Cont(flow);
        });
  }

  void Cont(FlowToken flow) {
    auto did_update = story_storage_->UpdateLinkValue(
        command_.path,
        [this](fidl::StringPtr* value) {
          std::string str_value;
          FXL_CHECK(fsl::StringFromVmo(*command_.value, &str_value));
          *value = str_value;
        },
        this /* context */);
    did_update->Then([this, did_update, flow](StoryStorage::Status status) {
      if (status == StoryStorage::Status::OK) {
        result_.status = fuchsia::modular::ExecuteStatus::OK;
      } else {
        result_.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
        std::stringstream stream;
        stream << "StoryStorage error status: " << (uint32_t)status;
        result_.error_message = stream.str();
      }
      Done(std::move(result_));
    });
  }

  SessionStorage* const session_storage_;
  fidl::StringPtr story_id_;
  fuchsia::modular::SetLinkValue command_;
  std::unique_ptr<StoryStorage> story_storage_;
  fuchsia::modular::ExecuteResult result_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SetLinkValueCall);
};

}  // namespace

SetLinkValueCommandRunner::SetLinkValueCommandRunner(
    SessionStorage* const session_storage)
    : CommandRunner(session_storage) {}

SetLinkValueCommandRunner::~SetLinkValueCommandRunner() = default;

void SetLinkValueCommandRunner::Execute(
    fidl::StringPtr story_id, fuchsia::modular::StoryCommand command,
    std::function<void(fuchsia::modular::ExecuteResult)> done) {
  FXL_CHECK(command.is_set_link_value());

  operation_queue_.Add(new SetLinkValueCall(
      session_storage_, std::move(story_id),
      std::move(command.set_link_value()), std::move(done)));
}

}  // namespace modular
