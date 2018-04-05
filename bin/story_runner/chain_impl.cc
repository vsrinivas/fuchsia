// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/story_runner/chain_impl.h"

#include "peridot/bin/story_runner/story_controller_impl.h"
#include "peridot/lib/fidl/clone.h"

namespace modular {

ChainImpl::ChainImpl(const fidl::VectorPtr<fidl::StringPtr>& path, const ChainData& chain_data) {
  for (const auto& i : *path) {
    path_->push_back(i);
  }
  chain_data.Clone(&chain_data_);
}

ChainImpl::~ChainImpl() = default;

LinkPathPtr ChainImpl::GetLinkPathForKey(const fidl::StringPtr& key) {
  auto it = std::find_if(
      chain_data_.key_to_link_map->begin(), chain_data_.key_to_link_map->end(),
      [&key](const ChainKeyToLinkData& data) { return data.key == key; });

  if (it == chain_data_.key_to_link_map->end())
    return nullptr;

  return CloneOptional(it->link_path);
}

}  // namespace modular
