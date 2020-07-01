// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_STORAGE_SESSION_STORAGE_H_
#define SRC_MODULAR_BIN_SESSIONMGR_STORAGE_SESSION_STORAGE_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>

#include <map>
#include <optional>

#include "src/modular/bin/sessionmgr/storage/story_storage.h"
#include "src/modular/lib/async/cpp/future.h"

using OnStoryDeletedCallback = fit::function<void(std::string story_id)>;
using OnStoryUpdatedCallback =
    fit::function<void(std::string story_id, fuchsia::modular::internal::StoryData story_data)>;
using OnAnnotationsUpdatedCallback = fit::function<void(
    std::string story_id, std::vector<fuchsia::modular::Annotation> annotations)>;

namespace modular {

// This class has the following responsibilities:
//
// * Manage in-memory metadata about what stories are part of a single session.
class SessionStorage {
 public:
  // Constructs a new SessionStorage with internal storage.
  SessionStorage();

  // |callback| is notified whenever a story has been deleted. This
  // notification is either the result of:
  //
  // a) The story being deleted on another device.
  // b) The story having been deleted locally with DeleteStory().
  void set_on_story_deleted(OnStoryDeletedCallback callback) {
    on_story_deleted_ = std::move(callback);
  }

  // |callback| is notified whenever a story has been added or updated.
  // Currently we do not differentiate between the two, and it is up to the
  // client to make this distinction.
  //
  // The update could be the result of a local modification (ie, through
  // Update*()) or a modification on another device.
  void set_on_story_updated(OnStoryUpdatedCallback callback) {
    on_story_updated_ = std::move(callback);
  }

  // |callback| is notified once |story_id|'s annotations are updated, then the callback is removed.
  // If the story does not exist, the callback will be invoked after the story is created and its
  // annotations are updated, but not for the initial set of annotations.
  //
  // The callback will also be removed when the story is deleted.
  void add_on_annotations_updated_once(std::string story_id,
                                       OnAnnotationsUpdatedCallback callback) {
    auto [it, inserted] = on_annotations_updated_callbacks_.try_emplace(
        story_id, std::vector<OnAnnotationsUpdatedCallback>{});
    it->second.push_back(std::move(callback));
  }

  // Creates a new story with the given name and returns |story_name|.
  std::string CreateStory(std::string story_name,
                          std::vector<fuchsia::modular::Annotation> annotations);

  // Deletes the |story_id| from the list of known stories.
  void DeleteStory(std::string story_id);

  // Sets the last focused timestamp for |story_id| to |ts|. Completes the
  // returned Future when done.
  void UpdateLastFocusedTimestamp(std::string story_id, int64_t ts);

  // Returns a StoryDataPtr for |story_id|. If |story_id| is not a valid
  // story, the returned StoryDataPtr will be null.
  fuchsia::modular::internal::StoryDataPtr GetStoryData(std::string story_id);

  // Returns a vector of StoryData for all stories in this session.
  std::vector<fuchsia::modular::internal::StoryData> GetAllStoryData();

  // Adds the given annotations for |story_id| to |annotations|. Existing annotations are not
  // removed, but existing annotations with the same key as a given annotation will be updated
  // with the value of the given annotation.
  //
  // Returns an optional AnnotationError that is either:
  //  * std::nullopt - successful merge
  //  * AnnotationError::TOO_MANY_ANNOTATIONS - the merge operation would result in too many
  //    annotations
  //  * AnnotationError::NOT_FOUND - the story does not exist
  //  * AnnotationError::VALUE_TOO_BIG - one of the annotations had a buffer value that
  //    exceeded the size limit
  std::optional<fuchsia::modular::AnnotationError> MergeStoryAnnotations(
      std::string story_name, std::vector<fuchsia::modular::Annotation> annotations);

  // Gets the StoryStorage for the story with the given |story_id| to perform
  // operations on the story such as adding modules, updating links, etc.
  std::shared_ptr<StoryStorage> GetStoryStorage(std::string story_id);

 private:
  // Invokes callbacks in |on_annotations_updated_callbacks_| for |story_id| to notify
  // listeners that annotations for the story have the new value |annotations|,
  // then removes all callbacks for the story.
  //
  // Does nothing if there are no callbacks registered for |story_id|.
  void NotifyAndRemoveOnAnnotationsUpdated(
      std::string story_id, const std::vector<fuchsia::modular::Annotation>& annotations);

  OnStoryDeletedCallback on_story_deleted_;
  OnStoryUpdatedCallback on_story_updated_;

  // Map of story_id to a callback invoked when that story's annotations are updated.
  std::map<std::string, std::vector<OnAnnotationsUpdatedCallback>>
      on_annotations_updated_callbacks_;

  // In-memory map from story_id to the corresponding StoryData.  This was
  // previously stored in the ledger.
  std::map<std::string, fuchsia::modular::internal::StoryData> story_data_backing_store_;

  // In-memory map from story_id to the corresponding StoryData.  This was
  // previously stored in the ledger.
  std::map<std::string, std::shared_ptr<StoryStorage>> story_storage_backing_store_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionStorage);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_STORAGE_SESSION_STORAGE_H_
