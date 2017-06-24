// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/acquirers/story_info/story_watcher_impl.h"

#include "apps/maxwell/src/acquirers/story_info/link_watcher_impl.h"
#include "apps/maxwell/src/acquirers/story_info/modular.h"
#include "apps/maxwell/src/acquirers/story_info/story_info.h"
#include "apps/maxwell/src/context_engine/scope_utils.h"
#include "apps/modular/lib/ledger/storage.h"

namespace maxwell {

StoryWatcherImpl::StoryWatcherImpl(StoryInfoAcquirer* const owner,
                                   ContextPublisher* const publisher,
                                   modular::StoryProvider* const story_provider,
                                   const std::string& story_id)
    : owner_(owner),
      publisher_(publisher),
      story_id_(story_id),
      story_watcher_binding_(this),
      story_links_watcher_binding_(this) {
  story_provider->GetController(story_id, story_controller_.NewRequest());

  story_controller_.set_connection_error_handler([this] {
      owner_->DropStoryWatcher(story_id_);
    });

  story_controller_->Watch(
      story_watcher_binding_.NewBinding());

  story_watcher_binding_.set_connection_error_handler([this] {
      owner_->DropStoryWatcher(story_id_);
    });

  story_controller_->GetActiveLinks(
      story_links_watcher_binding_.NewBinding(),
      [this](fidl::Array<modular::LinkPathPtr> links) {
        for (auto& link_path : links) {
          WatchLink(link_path);
        }
      });
}

StoryWatcherImpl::~StoryWatcherImpl() = default;

// |StoryWatcher|
void StoryWatcherImpl::OnStateChange(modular::StoryState new_state) {
  // TODO(jwnichols): Choose between recording the state here vs. the StoryProviderWatcher
  // once the bug in StoryProviderWatcher is fixed.
  std::string state_text = StoryStateToString(new_state);
  std::string state_json;
  modular::XdrWrite(&state_json, &state_text, modular::XdrFilter<std::string>);
  publisher_->Publish(CreateKey(story_id_, "state"), state_json);
}

// |StoryWatcher|
void StoryWatcherImpl::OnModuleAdded(modular::ModuleDataPtr module_data) {
  std::string meta;
  modular::XdrWrite(&meta, &module_data, XdrModuleData);
  publisher_->Publish(
      MakeModuleScopeTopic(story_id_, module_data->module_path, "meta"),
      meta);
}

// |StoryLinksWatcher|
void StoryWatcherImpl::OnNewLink(modular::LinkPathPtr link_path) {
  WatchLink(link_path);
}

void StoryWatcherImpl::WatchLink(const modular::LinkPathPtr& link_path) {
  links_.emplace(
      std::make_pair(
          modular::MakeLinkKey(link_path), std::make_unique<LinkWatcherImpl>(
              this, story_controller_.get(), publisher_, story_id_, link_path)));
}

void StoryWatcherImpl::DropLink(const std::string& link_key) {
  links_.erase(link_key);
}

}  // namespace maxwell
