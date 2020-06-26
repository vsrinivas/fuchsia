// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/modular/bin/sessionmgr/puppet_master/command_runners/operation_calls/add_mod_call.h"

#include <lib/fidl/cpp/clone.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/modular/lib/fidl/clone.h"

namespace modular {

namespace {

class AddModCall : public Operation<fuchsia::modular::ExecuteResult, fuchsia::modular::ModuleData> {
 public:
  AddModCall(StoryStorage* const story_storage, AddModParams add_mod_params, ResultCall done)
      : Operation("AddModCommandRunner::AddModCall", std::move(done)),
        story_storage_(story_storage),
        add_mod_params_(std::move(add_mod_params)) {}

 private:
  void Run() override {
    FlowToken flow{this, &out_result_, &out_module_data_};

    // Success status by default, it will be updated it if an error state is
    // found.
    out_result_.status = fuchsia::modular::ExecuteStatus::OK;

    if (add_mod_params_.intent.action.has_value() && !add_mod_params_.intent.handler.has_value()) {
      out_result_.status = fuchsia::modular::ExecuteStatus::NO_MODULES_FOUND;
      out_result_.error_message = "Module resolution via Intent.action is deprecated.";
      return;
    }

    WriteModuleData(flow);
  }

  // Write module data
  void WriteModuleData(FlowToken flow) {
    out_module_data_.set_module_url(add_mod_params_.intent.handler.value());
    out_module_data_.set_module_path(add_mod_params_.parent_mod_path);
    out_module_data_.mutable_module_path()->push_back(add_mod_params_.mod_name);
    out_module_data_.set_module_source(add_mod_params_.module_source);
    out_module_data_.set_module_deleted(false);
    if (!add_mod_params_.surface_relation) {
      out_module_data_.clear_surface_relation();
    } else {
      fidl::Clone(*add_mod_params_.surface_relation, out_module_data_.mutable_surface_relation());
    }
    out_module_data_.set_is_embedded(add_mod_params_.is_embedded);
    out_module_data_.set_intent(std::move(add_mod_params_.intent));

    // Operation stays alive until flow goes out of scope.
    fuchsia::modular::ModuleData module_data;
    out_module_data_.Clone(&module_data);
    story_storage_->WriteModuleData(std::move(module_data));
  }

  StoryStorage* const story_storage_;  // Not owned.
  modular::AddModParams add_mod_params_;
  fuchsia::modular::ModuleData out_module_data_;
  fuchsia::modular::ExecuteResult out_result_;
  // Used when creating the map info to execute an operation as soon as it
  // arrives.
  OperationCollection operations_;
  // Used to enqueue sub-operations that should be executed sequentially.
  OperationQueue operation_queue_;
};

}  // namespace

void AddAddModOperation(
    OperationContainer* const container, StoryStorage* const story_storage,
    AddModParams add_mod_params,
    fit::function<void(fuchsia::modular::ExecuteResult, fuchsia::modular::ModuleData)> done) {
  container->Add(
      std::make_unique<AddModCall>(story_storage, std::move(add_mod_params), std::move(done)));
}

}  // namespace modular
