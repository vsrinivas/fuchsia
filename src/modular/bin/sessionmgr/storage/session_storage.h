// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_STORAGE_SESSION_STORAGE_H_
#define SRC_MODULAR_BIN_SESSIONMGR_STORAGE_SESSION_STORAGE_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>

#include <map>
#include <optional>
#include <set>

#include "src/modular/bin/sessionmgr/storage/story_storage.h"
#include "src/modular/bin/sessionmgr/storage/watcher_list.h"
#include "src/modular/lib/async/cpp/future.h"

namespace modular {

// A callback passed to |SessionStorage.SubscribeStoryDeleted| that is called when the story
// |story_id| has been deleted.
//
// Returns a |WatchInterest| value that signals whether the callback should be deleted or
// kept after it has been called.
using StoryDeletedCallback = fit::function<WatchInterest(std::string story_id)>;

// A callback passed to |SessionStorage.SubscribeStoryUpdated| that is called when the story
// |story_id| has been updated.
//
// Returns a |WatchInterest| value that signals whether the callback should be deleted or
// kept after it has been called.
using StoryUpdatedCallback = fit::function<WatchInterest(
    std::string story_id, const fuchsia::modular::internal::StoryData& story_data)>;

// A callback passed to |SessionStorage.SubscribeAnnotationsUpdated| that is called when the
// annotations for story |story_id| have been updated or deleted.
//
// |annotations| contains the new, complete set of annotations.
// |annotation_keys_updated| contains keys of annotations that were added or had their value
//    set since last update.
// |annotation_keys_deleted| contains keys of annotations that have been deleted, i.e.
//    were present in the last update, and are no longer present in |annotations|.
using AnnotationsUpdatedCallback = fit::function<WatchInterest(
    std::string story_id, const std::vector<fuchsia::modular::Annotation>& annotations,
    const std::set<std::string>& annotation_keys_updated,
    const std::set<std::string>& annotation_keys_deleted)>;

// This class has the following responsibilities:
//
// * Manage in-memory metadata about what stories are part of a single session.
class SessionStorage {
 public:
  // Constructs a new SessionStorage with internal storage.
  SessionStorage() = default;

  // |callback| is notified whenever a story has been deleted. This
  // notification is either the result of:
  //
  // a) The story being deleted on another device.
  // b) The story having been deleted locally with DeleteStory().
  void SubscribeStoryDeleted(StoryDeletedCallback callback) {
    story_deleted_watchers_.Add(std::move(callback));
  }

  // |callback| is notified whenever a story has been added or updated.
  // Currently we do not differentiate between the two, and it is up to the
  // client to make this distinction.
  //
  // The update could be the result of a local modification (ie, through
  // Update*()) or a modification on another device.
  void SubscribeStoryUpdated(StoryUpdatedCallback callback) {
    story_updated_watchers_.Add(std::move(callback));
  }

  // |callback| is notified whenever a |story_id|'s annotations are updated or deleted.
  void SubscribeAnnotationsUpdated(AnnotationsUpdatedCallback callback) {
    annotations_updated_watchers_.Add(std::move(callback));
  }

  // Creates a new story with the given name and returns |story_name|.
  std::string CreateStory(std::string story_name,
                          std::vector<fuchsia::modular::Annotation> annotations);

  // Deletes the |story_id| from the list of known stories.
  void DeleteStory(std::string story_id);

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
  // Invokes callbacks in |story_updated_watchers_| to notify watchers that the story |story_id|'s
  // data was updated.
  //
  // The story must exist in |story_data_backing_store_|.
  void NotifyStoryUpdated(std::string story_id);

  // A list of callbacks invoked when a story is deleted.
  WatcherList<StoryDeletedCallback> story_deleted_watchers_;

  // A list of callbacks invoked when a story's StoryData is updated.
  WatcherList<StoryUpdatedCallback> story_updated_watchers_;

  // A list of callbacks invoked when a story's annotations are updated or deleted.
  WatcherList<AnnotationsUpdatedCallback> annotations_updated_watchers_;

  // In-memory map from story_id to the corresponding StoryData.
  std::map<std::string, fuchsia::modular::internal::StoryData> story_data_backing_store_;

  // In-memory map from story_id to the corresponding StoryStorage.
  std::map<std::string, std::shared_ptr<StoryStorage>> story_storage_backing_store_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionStorage);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_STORAGE_SESSION_STORAGE_H_
