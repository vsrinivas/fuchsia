// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/get_link_path_for_parameter_name_call.h"

#include "peridot/lib/fidl/clone.h"

namespace modular {

GetLinkPathForParameterNameCall::GetLinkPathForParameterNameCall(
    StoryStorage* const story_storage,
    fidl::VectorPtr<fidl::StringPtr> module_name, fidl::StringPtr link_name,
    ResultCall result_call)
    : Operation("AddModCommandRunner::GetLinkPathForParameterNameCall",
                std::move(result_call)),
      story_storage_(story_storage),
      module_name_(std::move(module_name)),
      link_name_(std::move(link_name)),
      link_path_(nullptr) {}

void GetLinkPathForParameterNameCall::Run() {
  FlowToken flow{this, &link_path_};
  story_storage_->ReadModuleData(module_name_)
      ->Then([this, flow](fuchsia::modular::ModuleDataPtr module_data) {
        if (!module_data) {
          return;
        }
        auto& param_map = module_data->parameter_map;
        auto it = std::find_if(
            param_map.entries->begin(), param_map.entries->end(),
            [this](const fuchsia::modular::ModuleParameterMapEntry& entry) {
              return entry.name == link_name_;
            });
        if (it != param_map.entries->end()) {
          link_path_ = CloneOptional(it->link_path);
        }

        if (!link_path_) {
          link_path_ = fuchsia::modular::LinkPath::New();
          link_path_->module_path = module_name_.Clone();
          link_path_->link_name = link_name_;
        }
        // Flow goes out of scope, finish operation returning link_path.
      });
}

}  // namespace modular
