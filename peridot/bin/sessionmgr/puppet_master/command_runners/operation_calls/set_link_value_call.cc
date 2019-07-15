// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/set_link_value_call.h"

namespace modular {

namespace {

class SetLinkValueCall : public Operation<fuchsia::modular::ExecuteResult> {
 public:
  SetLinkValueCall(StoryStorage* const story_storage, fuchsia::modular::LinkPath link_path,
                   fit::function<void(fidl::StringPtr*)> mutate_fn, ResultCall done)
      : Operation("SetLinkValueCall", std::move(done)),
        story_storage_(story_storage),
        link_path_(std::move(link_path)),
        mutate_fn_(std::move(mutate_fn)) {}

 private:
  void Run() override {
    FlowToken flow{this, &result_};
    auto did_update =
        story_storage_->UpdateLinkValue(link_path_, std::move(mutate_fn_), this /* context */);
    did_update->Then([this, flow](StoryStorage::Status status) {
      if (status == StoryStorage::Status::OK) {
        result_.status = fuchsia::modular::ExecuteStatus::OK;
      } else {
        result_.status = fuchsia::modular::ExecuteStatus::INTERNAL_ERROR;
        std::stringstream stream;
        stream << "StoryStorage error status: " << (uint32_t)status;
        result_.error_message = stream.str();
      }
    });
  }

  fidl::StringPtr story_id_;
  StoryStorage* const story_storage_;
  fuchsia::modular::LinkPath link_path_;
  fit::function<void(fidl::StringPtr*)> mutate_fn_;
  fuchsia::modular::ExecuteResult result_;
};

}  // namespace

void AddSetLinkValueOperation(OperationContainer* const operation_container,
                              StoryStorage* const story_storage,
                              fuchsia::modular::LinkPath link_path,
                              fit::function<void(fidl::StringPtr*)> mutate_fn,
                              fit::function<void(fuchsia::modular::ExecuteResult)> done) {
  operation_container->Add(std::make_unique<SetLinkValueCall>(
      story_storage, std::move(link_path), std::move(mutate_fn), std::move(done)));
}

}  // namespace modular
