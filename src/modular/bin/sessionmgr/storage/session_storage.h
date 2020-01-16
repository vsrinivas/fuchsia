// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_STORAGE_SESSION_STORAGE_H_
#define SRC_MODULAR_BIN_SESSIONMGR_STORAGE_SESSION_STORAGE_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>

#include <optional>

#include "src/modular/bin/sessionmgr/storage/story_storage.h"
#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/ledger_client/ledger_client.h"
#include "src/modular/lib/ledger_client/page_client.h"
#include "src/modular/lib/ledger_client/page_id.h"

namespace modular {

// This class has the following responsibilities:
//
// * Manage the persistence of metadata about what stories are part of a single
//   session.
// * Observe the metadata and call clients back when changes initiated by other
//   Ledger clients appear.
// * Manage the lifecycle of Ledger pages for storing individual story
//   metadata. The contents of these pages are governed by StoryStoage.
//
// All calls operate directly on the Ledger itself: no local caching is
// performed.
class SessionStorage : public PageClient {
 public:
  // Constructs a new SessionStorage with storage on |page_id| in the ledger
  // given by |ledger_client|.
  //
  // |ledger_client| must outlive *this.
  SessionStorage(LedgerClient* ledger_client, LedgerPageId page_id);

  // |callback| is notified whenever a story has been deleted. This
  // notification is either the result of:
  //
  // a) The story being deleted on another device.
  // b) The story having been deleted locally with DeleteStory().
  void set_on_story_deleted(fit::function<void(fidl::StringPtr story_id)> callback) {
    on_story_deleted_ = std::move(callback);
  }

  // |callback| is notified whenever a story has been added or updated.
  // Currently we do not differentiate between the two, and it is up to the
  // client to make this distinction.
  //
  // The update could be the result of a local modification (ie, through
  // Update*()) or a modification on another device.
  void set_on_story_updated(fit::function<void(fidl::StringPtr story_id,
                                               fuchsia::modular::internal::StoryData story_data)>
                                callback) {
    on_story_updated_ = std::move(callback);
  }

  // Creates a new story and returns a tuple of (story id, story ledger page
  // id) on completion. |story_name| may be null.
  //
  // If |story_name| is not provided, a UUID will be generated as the name.
  //
  // TODO(thatguy): Allowing for null story names is left in for backwards
  // compatibility with existing code. The intention is that all clients
  // outside the FW (through FIDL interfaces) use story names exclusively.  It
  // is unclear if internal story IDs should be an implementation detail of
  // SessionStorage, or if they should be exposed to the story runtime
  // architecture.
  FuturePtr<fidl::StringPtr, fuchsia::ledger::PageId> CreateStory(
      fidl::StringPtr story_name, std::vector<fuchsia::modular::Annotation> annotations);

  // Same as above, but defaults |story_name| to nullptr.
  FuturePtr<fidl::StringPtr, fuchsia::ledger::PageId> CreateStory(
      std::vector<fuchsia::modular::Annotation> annotations);

  // Deletes the |story_id| from the list of known stories and completes the
  // returned Future when done.
  FuturePtr<> DeleteStory(fidl::StringPtr story_id);

  // Sets the last focused timestamp for |story_id| to |ts|. Completes the
  // returned Future when done.
  FuturePtr<> UpdateLastFocusedTimestamp(fidl::StringPtr story_id, int64_t ts);

  // Returns a Future StoryDataPtr for |story_id|. If |story_id| is not a valid
  // story, the returned StoryDataPtr will be null.
  FuturePtr<fuchsia::modular::internal::StoryDataPtr> GetStoryData(fidl::StringPtr story_id);

  // Returns a Future vector of StoryData for all stories in this session.
  //
  // TODO(thatguy): If the return value grows large, an dispatcher stream would
  // be a more appropriate return value.
  FuturePtr<std::vector<fuchsia::modular::internal::StoryData>> GetAllStoryData();

  // DEPRECATED: Use MergeStoryAnnotations.
  // Sets the annotations for |story_id| to |annotations|. This overwrites all existing annotations.
  FuturePtr<> UpdateStoryAnnotations(fidl::StringPtr story_name,
                                     std::vector<fuchsia::modular::Annotation> annotations);

  // Adds the given annotations for |story_id| to |annotations|. Existing annotations are not
  // removed, but existing annotations with the same key as a given annotation will be updated
  // with the value of the given annotation.
  //
  // Returns a FuturePtr that resolves to one of:
  //  * optional<AnnotationError> has no value - successful merge
  //  * AnnotationError::TOO_MANY_ANNOTATIONS - the merge operation would result in too many
  //    annotations
  //  * AnnotationError::NOT_FOUND - the story was deleted, or could not be found or updated for
  //    some other reason
  FuturePtr<std::optional<fuchsia::modular::AnnotationError>> MergeStoryAnnotations(
      fidl::StringPtr story_name, std::vector<fuchsia::modular::Annotation> annotations);

  // Gets the StoryStorage for the story with the given |story_id| to perform
  // operations on the story such as adding modules, updating links, etc.
  FuturePtr<std::unique_ptr<StoryStorage>> GetStoryStorage(fidl::StringPtr story_id);

 private:
  // |PageClient|
  void OnPageChange(const std::string& key, const std::string& value) override;

  // |PageClient|
  void OnPageDelete(const std::string& key) override;

  LedgerClient* const ledger_client_;
  OperationQueue operation_queue_;

  fit::function<void(fidl::StringPtr story_id)> on_story_deleted_;
  fit::function<void(fidl::StringPtr story_id, fuchsia::modular::internal::StoryData story_data)>
      on_story_updated_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionStorage);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_STORAGE_SESSION_STORAGE_H_
