// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/session_storage.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <unordered_set>

#include "src/modular/bin/sessionmgr/annotations.h"
#include "src/modular/lib/fidl/clone.h"

namespace modular {

SessionStorage::SessionStorage() {}

std::string SessionStorage::CreateStory(std::string story_name,
                                        std::vector<fuchsia::modular::Annotation> annotations) {
  // Check if the story already exists.  If it does, this whole function should
  // behave as a noop.
  auto it = story_data_backing_store_.find(story_name);
  if (it == story_data_backing_store_.end()) {
    fuchsia::modular::internal::StoryData story_data({});
    story_data.set_story_name(story_name);
    story_data.mutable_story_info()->set_id(story_name);
    story_data.mutable_story_info()->set_last_focus_time(0);
    story_data.mutable_story_info()->set_annotations(std::move(annotations));

    fidl::StringPtr story_name_for_callback = story_name;
    fuchsia::modular::internal::StoryData callback_data;
    story_data.Clone(&callback_data);
    story_data_backing_store_[story_name] = std::move(story_data);

    if (on_story_updated_) {
      on_story_updated_(std::move(story_name_for_callback), std::move(callback_data));
    }
  }

  return story_name;
}

void SessionStorage::DeleteStory(fidl::StringPtr story_name) {
  auto it = story_data_backing_store_.find(story_name);
  if (it == story_data_backing_store_.end()) {
    return;
  }

  story_data_backing_store_.erase(it);
  story_storage_backing_store_.erase(story_name);
  if (on_story_deleted_) {
    on_story_deleted_(story_name);
  }

  return;
}

void SessionStorage::UpdateLastFocusedTimestamp(fidl::StringPtr story_name, const int64_t ts) {
  auto it = story_data_backing_store_.find(story_name);
  FX_DCHECK(it != story_data_backing_store_.end())
      << "SessionStorage::UpdateLastFocusedTimestamp was called on story " << story_name
      << " before it was created!";
  auto& story_data = it->second;

  if (story_data.story_info().last_focus_time() != ts) {
    story_data.mutable_story_info()->set_last_focus_time(ts);
    if (on_story_updated_) {
      on_story_updated_(std::move(story_name), std::move(story_data));
    }
  }
}

fuchsia::modular::internal::StoryDataPtr SessionStorage::GetStoryData(fidl::StringPtr story_name) {
  fuchsia::modular::internal::StoryDataPtr value{};
  auto it = story_data_backing_store_.find(story_name);
  if (it != story_data_backing_store_.end()) {
    value = fuchsia::modular::internal::StoryData::New();
    it->second.Clone(value.get());
  }
  return value;
}

// Returns a Future vector of StoryData for all stories in this session.
std::vector<fuchsia::modular::internal::StoryData> SessionStorage::GetAllStoryData() {
  std::vector<fuchsia::modular::internal::StoryData> vec{};
  for (auto it = story_data_backing_store_.begin(); it != story_data_backing_store_.end(); ++it) {
    fuchsia::modular::internal::StoryData val;
    it->second.Clone(&val);
    vec.push_back(std::move(val));
  }
  return vec;
}

std::optional<fuchsia::modular::AnnotationError> SessionStorage::MergeStoryAnnotations(
    fidl::StringPtr story_name, std::vector<fuchsia::modular::Annotation> annotations) {
  // Ensure the story exists.
  auto it = story_data_backing_store_.find(story_name);
  if (it == story_data_backing_store_.end()) {
    return fuchsia::modular::AnnotationError::NOT_FOUND;
  }

  auto& story_data = it->second;

  // Ensure that none of the annotations are too big.
  for (auto const& annotation : annotations) {
    if (annotation.value && annotation.value->is_buffer() &&
        annotation.value->buffer().size >
            fuchsia::modular::MAX_ANNOTATION_VALUE_BUFFER_LENGTH_BYTES) {
      return fuchsia::modular::AnnotationError::VALUE_TOO_BIG;
    }
  }

  // Merge annotations.
  auto new_annotations =
      story_data.story_info().has_annotations()
          ? annotations::Merge(std::move(*story_data.mutable_story_info()->mutable_annotations()),
                               std::move(annotations))
          : std::move(annotations);

  // Ensure that the number of annotations does not exceed the limit per story.
  if (new_annotations.size() > fuchsia::modular::MAX_ANNOTATIONS_PER_STORY) {
    return fuchsia::modular::AnnotationError::TOO_MANY_ANNOTATIONS;
  }

  // Mutate story in-place.
  // No need to write story data back to map because we modify it in place.
  story_data.mutable_story_info()->set_annotations(std::move(new_annotations));

  if (on_story_updated_) {
    fuchsia::modular::internal::StoryData story_data_copy;
    story_data.Clone(&story_data_copy);
    on_story_updated_(std::move(story_name), std::move(story_data_copy));
  }

  // The merge was successful.
  return std::nullopt;
}

std::shared_ptr<StoryStorage> SessionStorage::GetStoryStorage(fidl::StringPtr story_name) {
  std::shared_ptr<StoryStorage> value(nullptr);
  auto data_it = story_data_backing_store_.find(story_name);
  if (data_it != story_data_backing_store_.end()) {
    auto it = story_storage_backing_store_.find(story_name);
    if (it == story_storage_backing_store_.end()) {
      // If no refcounted StoryStorage exists for this story yet, create and insert one.
      story_storage_backing_store_[story_name] = std::make_shared<StoryStorage>();
    }

    value = story_storage_backing_store_[story_name];
  }
  return value;
}

}  // namespace modular
