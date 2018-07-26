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
  SetLinkValueCall(StoryStorage* const story_storage, fidl::StringPtr story_id,
                   fuchsia::modular::SetLinkValue command, ResultCall done)
      : Operation("SetLinkValueCommandRunner::SetLinkValueCall",
                  std::move(done)),
        story_id_(std::move(story_id)),
        story_storage_(story_storage),
        command_(std::move(command)) {}

 private:
  void Run() override {
    FlowToken flow{this, &result_};
    result_.story_id = story_id_;
    auto did_update = story_storage_->UpdateLinkValue(
        command_.path,
        [this](fidl::StringPtr* value) {
          std::string str_value;
          FXL_CHECK(fsl::StringFromVmo(*command_.value, &str_value));
          *value = str_value;
        },
        this /* context */);
    did_update->Then([this, flow](StoryStorage::Status status) {
      if (status == StoryStorage::Status::OK) {
        result_.status = fuchsia::modular::ExecuteStatus::OK;
      } else {
        result_.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
        std::stringstream stream;
        stream << "StoryStorage error status: " << (uint32_t)status;
        result_.error_message = stream.str();
      }
    });
  }

  fidl::StringPtr story_id_;
  StoryStorage* const story_storage_;
  fuchsia::modular::SetLinkValue command_;
  fuchsia::modular::ExecuteResult result_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SetLinkValueCall);
};

}  // namespace

SetLinkValueCommandRunner::SetLinkValueCommandRunner() = default;
SetLinkValueCommandRunner::~SetLinkValueCommandRunner() = default;

void SetLinkValueCommandRunner::Execute(
    fidl::StringPtr story_id, StoryStorage* const story_storage,
    fuchsia::modular::StoryCommand command,
    std::function<void(fuchsia::modular::ExecuteResult)> done) {
  FXL_CHECK(command.is_set_link_value());

  operation_queue_.Add(new SetLinkValueCall(story_storage, std::move(story_id),
                                            std::move(command.set_link_value()),
                                            std::move(done)));
}

}  // namespace modular
