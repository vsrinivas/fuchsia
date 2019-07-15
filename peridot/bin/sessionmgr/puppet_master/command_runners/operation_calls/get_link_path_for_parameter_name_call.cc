// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/get_link_path_for_parameter_name_call.h"

#include "peridot/lib/fidl/clone.h"

namespace modular {

namespace {

class GetLinkPathForParameterNameCall : public Operation<fuchsia::modular::LinkPathPtr> {
 public:
  GetLinkPathForParameterNameCall(StoryStorage* const story_storage,
                                  std::vector<std::string> module_name, std::string link_name,
                                  ResultCall result_call)
      : Operation("AddModCommandRunner::GetLinkPathForParameterNameCall", std::move(result_call)),
        story_storage_(story_storage),
        module_name_(std::move(module_name)),
        link_name_(std::move(link_name)),
        link_path_(nullptr) {}

 private:
  void Run() override {
    FlowToken flow{this, &link_path_};
    story_storage_->ReadModuleData(module_name_)
        ->Then([this, flow](fuchsia::modular::ModuleDataPtr module_data) {
          if (!module_data) {
            return;
          }
          auto& param_map = module_data->parameter_map;
          auto it = std::find_if(param_map.entries.begin(), param_map.entries.end(),
                                 [this](const fuchsia::modular::ModuleParameterMapEntry& entry) {
                                   return entry.name == link_name_;
                                 });
          if (it != param_map.entries.end()) {
            link_path_ = CloneOptional(it->link_path);
          }

          if (!link_path_) {
            link_path_ = fuchsia::modular::LinkPath::New();
            link_path_->module_path = module_name_;
            link_path_->link_name = link_name_;
          }
          // Flow goes out of scope, finish operation returning link_path.
        });
  }

  StoryStorage* const story_storage_;  // Not owned.
  std::vector<std::string> module_name_;
  std::string link_name_;
  fuchsia::modular::LinkPathPtr link_path_;
};

}  // namespace

void AddGetLinkPathForParameterNameOperation(
    OperationContainer* const operation_container, StoryStorage* const story_storage,
    std::vector<std::string> module_name, std::string link_name,
    fit::function<void(fuchsia::modular::LinkPathPtr)> result_call) {
  operation_container->Add(std::make_unique<GetLinkPathForParameterNameCall>(
      story_storage, std::move(module_name), std::move(link_name), std::move(result_call)));
}

}  // namespace modular
