// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/session_storage.h"

#include <lib/fidl/cpp/clone.h>
#include <zircon/status.h>

#include <unordered_set>

#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/uuid/uuid.h"
#include "src/modular/bin/sessionmgr/annotations.h"
#include "src/modular/bin/sessionmgr/storage/constants_and_utils.h"
#include "src/modular/lib/fidl/clone.h"

namespace modular {

SessionStorage::SessionStorage() {
}

FuturePtr<fidl::StringPtr> SessionStorage::CreateStory(
    fidl::StringPtr story_name, std::vector<fuchsia::modular::Annotation> annotations) {

  if (!story_name || story_name->empty()) {
    story_name = uuid::Generate();
  }

  // Check if the story already exists.  If it does, this whole function should
  // behave as a noop.
  auto it = story_data_backing_store_.find(story_name);
  if (it == story_data_backing_store_.end()) {
    fuchsia::modular::internal::StoryData story_data({});
    story_data.set_story_name(story_name.value_or(""));
    story_data.mutable_story_info()->set_id(story_name.value_or(""));
    story_data.mutable_story_info()->set_last_focus_time(0);
    if (!annotations.empty()) {
      story_data.mutable_story_info()->set_annotations(std::move(annotations));
    }

    fidl::StringPtr story_name_for_callback = story_name;
    fuchsia::modular::internal::StoryData callback_data;
    story_data.Clone(&callback_data);
    story_data_backing_store_[story_name] = std::move(story_data);

    if (on_story_updated_) {
      on_story_updated_(std::move(story_name_for_callback), std::move(callback_data));
    }
  }

  return Future<fidl::StringPtr>::CreateCompleted("SessionStorage.CreateStory.ret",
                                                  std::move(story_name));
}

FuturePtr<fidl::StringPtr> SessionStorage::CreateStory(
    std::vector<fuchsia::modular::Annotation> annotations) {
  return CreateStory(/*story_name=*/nullptr, std::move(annotations));
}

FuturePtr<> SessionStorage::DeleteStory(fidl::StringPtr story_name) {
  auto it = story_data_backing_store_.find(story_name);
  if (it != story_data_backing_store_.end()) {
    story_data_backing_store_.erase(it);
  }

  auto storage_it = story_storage_backing_store_.find(story_name);
  if (storage_it != story_storage_backing_store_.end()) {
    story_storage_backing_store_.erase(storage_it);
  }

  if (on_story_deleted_) {
    on_story_deleted_(story_name);
  }

  return Future<>::CreateCompleted("SessionStorage.DeleteStory.ret");
}

FuturePtr<> SessionStorage::UpdateLastFocusedTimestamp(fidl::StringPtr story_name,
                                                       const int64_t ts) {
  auto it = story_data_backing_store_.find(story_name);
  FX_DCHECK(it != story_data_backing_store_.end())
      << "SessionStorage::UpdateLastFocusedTimestamp was called on story "
      << story_name << " before it was created!";
  auto& story_data = it->second;

  if (story_data.story_info().last_focus_time() != ts) {
    story_data.mutable_story_info()->set_last_focus_time(ts);
    if (on_story_updated_) {
      on_story_updated_(std::move(story_name), std::move(story_data));
    }
  }

  return Future<>::CreateCompleted("SessionStorage.UpdateLastFocusedTimestamp.ret");
}

FuturePtr<fuchsia::modular::internal::StoryDataPtr> SessionStorage::GetStoryData(
    fidl::StringPtr story_name) {
  fuchsia::modular::internal::StoryDataPtr value{};
  auto it = story_data_backing_store_.find(story_name);
  if (it != story_data_backing_store_.end()) {
    value = fuchsia::modular::internal::StoryData::New();
    it->second.Clone(value.get());
  }

  return Future<fuchsia::modular::internal::StoryDataPtr>::CreateCompleted(
      "SessionStorage.GetStoryData.ret", std::move(value));
}

// Returns a Future vector of StoryData for all stories in this session.
FuturePtr<std::vector<fuchsia::modular::internal::StoryData>> SessionStorage::GetAllStoryData() {
  std::vector<fuchsia::modular::internal::StoryData> vec{};

  for (auto it = story_data_backing_store_.begin(); it != story_data_backing_store_.end(); ++it) {
    fuchsia::modular::internal::StoryData val;
    it->second.Clone(&val);
    vec.push_back(std::move(val));
  }

  auto ret = Future<std::vector<fuchsia::modular::internal::StoryData>>::CreateCompleted(
      "SessionStorage.GetAllStoryData.ret", std::move(vec));
  return ret;
}

FuturePtr<> SessionStorage::UpdateStoryAnnotations(
    fidl::StringPtr story_name, std::vector<fuchsia::modular::Annotation> annotations) {
  auto it = story_data_backing_store_.find(story_name);
  if (it != story_data_backing_store_.end()) {
    fuchsia::modular::internal::StoryData& val = it->second;
    val.mutable_story_info()->set_annotations(std::move(annotations));
    fuchsia::modular::internal::StoryData val_copy;
    val.Clone(&val_copy);

    if (on_story_updated_) {
      on_story_updated_(std::move(story_name), std::move(val_copy));
    }
  }

  auto ret = Future<>::CreateCompleted("SessionStorage.UpdateStoryAnnotations.ret");
  return ret;
}

FuturePtr<std::optional<fuchsia::modular::AnnotationError>> SessionStorage::MergeStoryAnnotations(
    fidl::StringPtr story_name, std::vector<fuchsia::modular::Annotation> annotations) {
  // On success, this optional AnnotationError response will have no value (!has_value()).
  // Otherwise, the error will be set explicitly, or it is assumed the story is no longer viable
  // (default NOT_FOUND).

  std::optional<fuchsia::modular::AnnotationError> error = std::nullopt;
  // Get story data for this story, if it exists.  If not, then return
  // NOT_FOUND.
  auto it = story_data_backing_store_.find(story_name);
  if (it == story_data_backing_store_.end()) {
    // If the story doesn't exist, it was deleted.
    error = fuchsia::modular::AnnotationError::NOT_FOUND;
  } else {
    auto& story_data = it->second;

    // Merge annotations.
    auto new_annotations =
        story_data.story_info().has_annotations() ?
        annotations::Merge(
            std::move(*story_data.mutable_story_info()->mutable_annotations()),
            std::move(annotations))
        : std::move(annotations);

    // Mutate story in-place.
    if (new_annotations.size() > fuchsia::modular::MAX_ANNOTATIONS_PER_STORY) {
      error = fuchsia::modular::AnnotationError::TOO_MANY_ANNOTATIONS;
    } else {
      story_data.mutable_story_info()->set_annotations(std::move(new_annotations));
    }

    // No need to write story data back to map because we modified it in place.
  }

  auto ret = Future<std::optional<fuchsia::modular::AnnotationError>>::CreateCompleted(
      "SessionStorage.MergeStoryAnnotations.ret", std::move(error));
  return ret;
}

FuturePtr<std::shared_ptr<StoryStorage>> SessionStorage::GetStoryStorage(
    fidl::StringPtr story_name) {

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

  auto returned_future = Future<std::shared_ptr<StoryStorage>>::CreateCompleted(
      "SessionStorage.GetStoryStorage.returned_future", std::move(value));

  return returned_future;
}

}  // namespace modular
