// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/set_link_value_call.h"

namespace modular {

SetLinkValueCall::SetLinkValueCall(
    StoryStorage* const story_storage, fuchsia::modular::LinkPath link_path,
    std::function<void(fidl::StringPtr*)> mutate_fn, ResultCall done)
    : Operation("SetLinkValueCall", std::move(done)),
      story_storage_(story_storage),
      link_path_(std::move(link_path)),
      mutate_fn_(std::move(mutate_fn)) {}

void SetLinkValueCall::Run() {
  FlowToken flow{this, &result_};
  auto did_update = story_storage_->UpdateLinkValue(link_path_, mutate_fn_,
                                                    this /* context */);
  did_update->Then([this, flow](StoryStorage::Status status) {
    if (status == StoryStorage::Status::OK) {
      result_.status = fuchsia::modular::ExecuteStatus::OK;
    } else if (status == StoryStorage::Status::LINK_INVALID_JSON) {
      result_.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
      result_.error_message = "Attempted to update link with invalid JSON";
    } else {
      result_.status = fuchsia::modular::ExecuteStatus::INTERNAL_ERROR;
      std::stringstream stream;
      stream << "StoryStorage error status: " << (uint32_t)status;
      result_.error_message = stream.str();
    }
  });
}

}  // namespace modular
