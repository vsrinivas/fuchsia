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
      context_value_(writer),
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

  auto value = ContextValue::New();
  value->type = ContextValueType::STORY;
  value->meta = context_metadata_.Clone();

  context_value_.Set(std::move(value));
  context_value_.OnId([this](const fidl::String& value_id) {
    // We have to wait until here to watch the links so that
    // we have |context_value_id_| available to pass to each
    // watcher.
    story_controller_->GetActiveLinks(
        story_links_watcher_binding_.NewBinding(),
        [this](fidl::Array<modular::LinkPathPtr> links) {
          for (auto& link_path : links) {
            WatchLink(link_path);
          }
        });
  });
}

StoryWatcherImpl::~StoryWatcherImpl() = default;

// |StoryWatcher|
void StoryWatcherImpl::OnStateChange(modular::StoryState new_state) {
  // TODO(thatguy): Add recording of state to StoryMetadata.
}

// |StoryWatcher|
void StoryWatcherImpl::OnModuleAdded(modular::ModuleDataPtr module_data) {
  auto value = ContextValue::New();
  value->type = ContextValueType::MODULE;
  value->meta = ContextMetadataBuilder()
                    .SetModuleUrl(module_data->module_url)
                    .SetModulePath(module_data->module_path.Clone())
                    .Build();

  auto path = modular::EncodeModulePath(module_data->module_path);
  context_value_.OnId(
      fxl::MakeCopyable([ this, value = std::move(value),
                          path ](const fidl::String& value_id) mutable {
        auto it =
            module_values_.emplace(path, ScopedContextValue(writer_, value_id))
                .first;
        it->second.Set(std::move(value));
      }));
}

// |StoryLinksWatcher|
void StoryWatcherImpl::OnNewLink(modular::LinkPathPtr link_path) {
  WatchLink(link_path);
}

void StoryWatcherImpl::WatchLink(const modular::LinkPathPtr& link_path) {
  context_value_.OnId(fxl::MakeCopyable([ this, link_path = link_path.Clone() ](
      const fidl::String& value_id) {
    links_.emplace(std::make_pair(modular::MakeLinkKey(link_path),
                                  std::make_unique<LinkWatcherImpl>(
                                      this, story_controller_.get(), writer_,
                                      story_id_, value_id, link_path)));
  }));
}

void StoryWatcherImpl::OnFocusChange(bool focused) {
  context_metadata_->story->focused->state =
      focused ? FocusedState::State::FOCUSED : FocusedState::State::NOT_FOCUSED;
  UpdateContext();
}

void StoryWatcherImpl::OnStoryStateChange(modular::StoryInfoPtr info,
                                          modular::StoryState state) {
  // TODO(thatguy): Record this state too.
}

void StoryWatcherImpl::DropLink(const std::string& link_key) {
  links_.erase(link_key);
}

void StoryWatcherImpl::UpdateContext() {
  context_value_.UpdateMetadata(context_metadata_.Clone());
}

}  // namespace maxwell
