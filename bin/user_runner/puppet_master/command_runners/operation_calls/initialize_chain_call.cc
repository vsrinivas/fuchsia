// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/initialize_chain_call.h"

namespace modular {

InitializeChainCall::InitializeChainCall(
    StoryStorage* const story_storage,
    fidl::VectorPtr<fidl::StringPtr> module_path,
    fuchsia::modular::CreateModuleParameterMapInfoPtr create_parameter_map_info,
    ResultCall result_call)
    : Operation("InitializeChainCall", std::move(result_call)),
      story_storage_(story_storage),
      module_path_(std::move(module_path)),
      create_parameter_map_info_(std::move(create_parameter_map_info)) {}

void InitializeChainCall::Run() {
  FlowToken flow{this, &result_};

  result_ = fuchsia::modular::ModuleParameterMap::New();
  result_->entries.resize(0);

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
      for (const auto& i : *module_path_) {
        mapping->link_path.module_path.push_back(i);
      }
      mapping->link_path.link_name = key;

      // We issue N UpdateLinkValue calls and capture |flow| on each. We rely
      // on the fact that once all refcounted instances of |flow| are
      // destroyed, the InitializeChainCall will automatically finish.
      auto initial_data = info.create_link().initial_data;
      // TODO(miguelfrde): UpdateLinkValue can return an error StoryStatus. We
      // should handle it.
      story_storage_->UpdateLinkValue(
          mapping->link_path,
          [initial_data, flow](fidl::StringPtr* value) {
            if (value->is_null()) {
              // This is a new link. If it weren't, *value would be set to
              // some valid JSON.
              *value = initial_data;
            }
          },
          this /* context */);
    }

    result_->entries.push_back(std::move(*mapping));
  }
}

}  // namespace modular
