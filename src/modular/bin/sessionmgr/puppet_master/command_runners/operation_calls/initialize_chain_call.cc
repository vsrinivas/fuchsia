// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/modular/bin/sessionmgr/puppet_master/command_runners/operation_calls/initialize_chain_call.h"

#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/modular/bin/sessionmgr/puppet_master/command_runners/operation_calls/set_link_value_call.h"

namespace modular {

namespace {

// Populates a fuchsia::modular::ModuleParameterMap struct from a
// fuchsia::modular::CreateModuleParameterMapInfo struct. May create new Links
// for any fuchsia::modular::CreateModuleParameterMapInfo.property_info if
// property_info[i].is_create_link_info().
class InitializeChainCall
    : public Operation<fuchsia::modular::ExecuteResult, fuchsia::modular::ModuleParameterMapPtr> {
 public:
  InitializeChainCall(StoryStorage* const story_storage, std::vector<std::string> module_path,
                      fuchsia::modular::CreateModuleParameterMapInfoPtr create_parameter_map_info,
                      ResultCall result_call)
      : Operation("InitializeChainCall", std::move(result_call)),
        story_storage_(story_storage),
        module_path_(std::move(module_path)),
        create_parameter_map_info_(std::move(create_parameter_map_info)) {}

 private:
  void Run() override {
    FlowToken flow{this, &result_, &parameter_map_};

    parameter_map_ = fuchsia::modular::ModuleParameterMap::New();
    parameter_map_->entries.resize(0);

    if (!create_parameter_map_info_) {
      return;
    }

    // For each property in |create_parameter_map_info_|, either:
    // a) Copy the |link_path| to |result_| directly or
    // b) Create & populate a new Link and add the correct mapping to
    // |result_|.
    for (auto& entry : *create_parameter_map_info_->property_info) {
      const auto& key = entry.key;
      const auto& info = entry.value;

      auto mapping = fuchsia::modular::ModuleParameterMapEntry::New();
      mapping->name = key;
      if (info.is_link_path()) {
        info.link_path().Clone(&mapping->link_path);
      } else {  // info->is_create_link()
        mapping->link_path.module_path.resize(0);
        for (const auto& i : module_path_) {
          mapping->link_path.module_path.push_back(i);
        }
        mapping->link_path.link_name = key;

        // We issue N UpdateLinkValue calls and capture |flow| on each. We rely
        // on the fact that once all refcounted instances of |flow| are
        // destroyed, the InitializeChainCall will automatically finish.
        std::string initial_json;
        if (info.create_link().initial_data.size > 0) {
          FX_CHECK(fsl::StringFromVmo(info.create_link().initial_data, &initial_json));
        }
        fuchsia::modular::LinkPath out_path;
        mapping->link_path.Clone(&out_path);
        AddSetLinkValueOperation(
            &operations_, story_storage_, std::move(out_path),
            [initial_json](fidl::StringPtr* value) { *value = initial_json; },
            [this, flow](fuchsia::modular::ExecuteResult result) {
              if (result.status != fuchsia::modular::ExecuteStatus::OK) {
                result_ = std::move(result);
              }
            });
      }

      parameter_map_->entries.push_back(std::move(*mapping));
    }
  }

  StoryStorage* const story_storage_;
  const std::vector<std::string> module_path_;
  const fuchsia::modular::CreateModuleParameterMapInfoPtr create_parameter_map_info_;
  fuchsia::modular::ModuleParameterMapPtr parameter_map_;
  fuchsia::modular::ExecuteResult result_;
  OperationCollection operations_;
};

}  // namespace

void AddInitializeChainOperation(
    OperationContainer* const operation_container, StoryStorage* const story_storage,
    std::vector<std::string> module_path,
    fuchsia::modular::CreateModuleParameterMapInfoPtr create_parameter_map_info,
    fit::function<void(fuchsia::modular::ExecuteResult, fuchsia::modular::ModuleParameterMapPtr)>
        result_call) {
  operation_container->Add(std::make_unique<InitializeChainCall>(
      story_storage, std::move(module_path), std::move(create_parameter_map_info),
      std::move(result_call)));
}

}  // namespace modular
