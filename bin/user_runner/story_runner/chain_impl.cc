// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/story_runner/chain_impl.h"

#include "peridot/lib/fidl/clone.h"

namespace fuchsia {
namespace modular {

ChainImpl::ChainImpl(const fidl::VectorPtr<fidl::StringPtr>& path,
                     const ModuleParameterMap& parameter_map) {
  for (const auto& i : *path) {
    path_->push_back(i);
  }
  parameter_map.Clone(&parameter_map_);
}

ChainImpl::~ChainImpl() = default;

LinkPathPtr ChainImpl::GetLinkPathForParameterName(
    const fidl::StringPtr& name) {
  auto it = std::find_if(parameter_map_.entries->begin(),
                         parameter_map_.entries->end(),
                         [&name](const ModuleParameterMapEntry& data) {
                           return data.name == name;
                         });

  if (it == parameter_map_.entries->end()) {
    return nullptr;
  }
  return CloneOptional(it->link_path);
}

}  // namespace modular
}  // namespace fuchsia
