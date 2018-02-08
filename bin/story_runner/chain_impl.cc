// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/story_runner/chain_impl.h"

#include "peridot/bin/story_runner/story_controller_impl.h"

namespace modular {

ChainImpl::ChainImpl(fidl::Array<fidl::String> path,
                     ChainDataPtr chain_data,
                     StoryController* const story_controller)
    : path_(std::move(path)),
      chain_data_(std::move(chain_data)),
      story_controller_(story_controller) {
  FXL_DCHECK(story_controller_ != nullptr);
}
ChainImpl::~ChainImpl() = default;

void ChainImpl::Connect(fidl::InterfaceRequest<Chain> request) {
  bindings_.AddBinding(this, std::move(request));
}

LinkPathPtr ChainImpl::GetLinkPathForKey(const fidl::String& key) {
  auto it = std::find_if(
      chain_data_->key_to_link_map.begin(), chain_data_->key_to_link_map.end(),
      [&key](const ChainKeyToLinkDataPtr& data) { return data->key == key; });
  if (it == chain_data_->key_to_link_map.end())
    return nullptr;

  return (*it)->link_path.Clone();
}

void ChainImpl::GetKeys(const GetKeysCallback& done) {
  fidl::Array<fidl::String> keys =
      fidl::Array<fidl::String>::New(chain_data_->key_to_link_map.size());
  auto it = chain_data_->key_to_link_map.begin();
  for (int i = 0; it != chain_data_->key_to_link_map.end(); ++i, ++it) {
    keys[i] = (*it)->key;
  }
  done(std::move(keys));
}

void ChainImpl::GetLink(const fidl::String& key,
                        fidl::InterfaceRequest<Link> request) {
  auto link_path = GetLinkPathForKey(key);
  if (!link_path)
    return;

  story_controller_->GetLink(std::move(link_path->module_path),
                             link_path->link_name, std::move(request));
}

}  // namespace modular
