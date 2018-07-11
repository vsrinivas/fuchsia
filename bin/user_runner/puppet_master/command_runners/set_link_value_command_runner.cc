// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/set_link_value_command_runner.h"

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/functional/make_copyable.h>

namespace modular {

SetLinkValueCommandRunner::SetLinkValueCommandRunner(
    SessionStorage* const session_storage)
    : CommandRunner(session_storage) {}

SetLinkValueCommandRunner::~SetLinkValueCommandRunner() = default;

void SetLinkValueCommandRunner::Execute(
    fidl::StringPtr story_id, fuchsia::modular::StoryCommand command,
    std::function<void(fuchsia::modular::ExecuteResult)> done) {
  FXL_CHECK(command.is_set_link_value());

  session_storage_->GetStoryStorage(story_id)
      ->AsyncMap(fxl::MakeCopyable(
          [this, story_id, command = std::move(command.set_link_value()),
           done =
               std::move(done)](std::unique_ptr<StoryStorage> story_storage) {
            if (!story_storage) {
              fuchsia::modular::ExecuteResult result;
              result.status = fuchsia::modular::ExecuteStatus::INVALID_STORY_ID;
              result.story_id = story_id;
              result.error_message = "No StoryStorage for given story.";
              return Future<fuchsia::modular::ExecuteResult>::CreateCompleted(
                  "SetLinkValueCommandRunner.Execute.GetStoryStorage.AsyncMap",
                  std::move(result));
            }
            std::string value;
            FXL_CHECK(fsl::StringFromVmo(*command.value, &value));
            return UpdateLinkValue(std::move(story_storage), story_id,
                                   std::move(command.path), std::move(value));
          }))
      ->Then([done = std::move(done)](fuchsia::modular::ExecuteResult result) {
        done(std::move(result));
      });
}

FuturePtr<fuchsia::modular::ExecuteResult>
SetLinkValueCommandRunner::UpdateLinkValue(
    std::unique_ptr<StoryStorage> story_storage,
    const fidl::StringPtr& story_id, const fuchsia::modular::LinkPath& path,
    std::string new_value) {
  FuturePtr<StoryStorage::Status> future = story_storage->UpdateLinkValue(
      path, [new_value](fidl::StringPtr* value) { *value = new_value; },
      this /* context */);
  return future->Map(
      fxl::MakeCopyable([this, story_storage = std::move(story_storage),
                         story_id](StoryStorage::Status status) {
        fuchsia::modular::ExecuteResult result;
        result.story_id = story_id;
        if (status == StoryStorage::Status::OK) {
          result.status = fuchsia::modular::ExecuteStatus::OK;
        } else {
          result.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
          std::stringstream stream;
          stream << "StoryStorage error status:" << (uint32_t)status;
          result.error_message = stream.str();
        }
        return result;
      }));
}

}  // namespace modular
