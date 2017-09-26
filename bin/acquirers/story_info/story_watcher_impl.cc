// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/acquirers/story_info/story_watcher_impl.h"

#include "lib/context/cpp/context_metadata_builder.h"
#include "lib/context/cpp/formatting.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/acquirers/story_info/link_watcher_impl.h"
#include "peridot/bin/acquirers/story_info/modular.h"
#include "peridot/bin/acquirers/story_info/story_info.h"
#include "peridot/lib/ledger/storage.h"

namespace maxwell {

StoryWatcherImpl::StoryWatcherImpl(StoryInfoAcquirer* const owner,
                                   ContextWriter* const writer,
                                   modular::StoryProvider* const story_provider,
                                   const std::string& story_id)
    : owner_(owner),
      writer_(writer),
      story_id_(story_id),
      story_watcher_binding_(this),
      story_links_watcher_binding_(this) {
  story_provider->GetController(story_id, story_controller_.NewRequest());

  story_controller_.set_connection_error_handler(
      [this] { owner_->DropStoryWatcher(story_id_); });

  story_controller_->Watch(story_watcher_binding_.NewBinding());

  story_watcher_binding_.set_connection_error_handler(
      [this] { owner_->DropStoryWatcher(story_id_); });

  context_metadata_ = ContextMetadataBuilder()
                          .SetStoryId(story_id)
                          .SetStoryFocused(false)
                          .Build();
  // TODO(thatguy): Add modular.StoryState.
  // TODO(thatguy): Add visible state.

  writer_->CreateValue(context_value_.NewRequest(), ContextValueType::STORY);
  context_value_->Set(nullptr /* content */, context_metadata_.Clone());

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
  // TODO(thatguy): Add recording of state to StoryMetadata.
}

// |StoryWatcher|
void StoryWatcherImpl::OnModuleAdded(modular::ModuleDataPtr module_data) {
  ContextValueWriterPtr module_value;
  context_value_->CreateChildValue(module_value.NewRequest(), ContextValueType::MODULE);
  module_value->Set(nullptr /* content */, ContextMetadataBuilder()
                    .SetModuleUrl(module_data->module_url)
                    .SetModulePath(module_data->module_path.Clone())
                    .Build());

  auto path = modular::EncodeModulePath(module_data->module_path);
  module_values_.emplace(path, std::move(module_value));
}

// |StoryLinksWatcher|
void StoryWatcherImpl::OnNewLink(modular::LinkPathPtr link_path) {
  WatchLink(link_path);
}

void StoryWatcherImpl::WatchLink(const modular::LinkPathPtr& link_path) {
  links_.emplace(std::make_pair(modular::MakeLinkKey(link_path),
                                std::make_unique<LinkWatcherImpl>(
                                    this, story_controller_.get(), story_id_,
                                    context_value_.get(), link_path)));
}

void StoryWatcherImpl::OnFocusChange(bool focused) {
  context_metadata_ = ContextMetadataBuilder(std::move(context_metadata_))
                          .SetStoryFocused(focused)
                          .Build();
  context_value_->Set(nullptr /* content */, context_metadata_.Clone());
}

void StoryWatcherImpl::OnStoryStateChange(modular::StoryInfoPtr info,
                                          modular::StoryState state) {
  // TODO(thatguy): Record this state too.
}

void StoryWatcherImpl::DropLink(const std::string& link_key) {
  links_.erase(link_key);
}

}  // namespace maxwell
