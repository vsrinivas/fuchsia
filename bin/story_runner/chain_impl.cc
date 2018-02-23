// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/story_runner/chain_impl.h"

#include "peridot/bin/story_runner/story_controller_impl.h"

namespace modular {

ChainImpl::ChainImpl(f1dl::Array<f1dl::String> path, ChainDataPtr chain_data)
    : path_(std::move(path)), chain_data_(std::move(chain_data)) {}
ChainImpl::~ChainImpl() = default;

LinkPathPtr ChainImpl::GetLinkPathForKey(const f1dl::String& key) {
  auto it = std::find_if(
      chain_data_->key_to_link_map.begin(), chain_data_->key_to_link_map.end(),
      [&key](const ChainKeyToLinkDataPtr& data) { return data->key == key; });
  if (it == chain_data_->key_to_link_map.end())
    return nullptr;

  return (*it)->link_path.Clone();
}

}  // namespace modular
