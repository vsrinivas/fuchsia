// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_STORAGE_SESSION_STORAGE_H_
#define PERIDOT_BIN_USER_RUNNER_STORAGE_SESSION_STORAGE_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/async/cpp/future.h>

#include "peridot/bin/user_runner/storage/story_storage.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/page_id.h"

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
  void set_on_story_deleted(
      std::function<void(fidl::StringPtr story_id)> callback) {
    on_story_deleted_ = std::move(callback);
  }

  // |callback| is notified whenever a story has been added or updated.
  // Currently we do not differentiate between the two, and it is up to the
  // client to make this distinction.
  //
  // The update could be the result of a local modification (ie, through
  // Update*()) or a modification on another device.
  void set_on_story_updated(
      std::function<void(fidl::StringPtr story_id,
                         fuchsia::modular::internal::StoryData story_data)>
          callback) {
    on_story_updated_ = std::move(callback);
  }

  // Creates a new story and returns a tuple of (story id, story ledger page
  // id) on completion. |story_name| and |extra_info| may be null.
  //
  // If |story_name| is set, it is associated as a client-supplied alias for
  // the story's record. Its data is retrievable through GetStoryDataByName().
  //
  // If |extra_info| is set, populates StoryData.story_info.extra with the
  // entries given.
  //
  // TODO(thatguy): Allowing for null story names is left in for backwards
  // compatibility with existing code. The intention is that all clients
  // outside the FW (through FIDL interfaces) use story names exclusively.  It
  // is unclear if internal story IDs should be an implementation detail of
  // SessionStorage, or if they should be exposed to the story runtime
  // architecture.
  FuturePtr<fidl::StringPtr, fuchsia::ledger::PageId> CreateStory(
      fidl::StringPtr story_name,
      fidl::VectorPtr<fuchsia::modular::StoryInfoExtraEntry> extra_info,
      bool is_kind_of_proto_story);

  // Same as above, but defaults |story_name| to nullptr.
  FuturePtr<fidl::StringPtr, fuchsia::ledger::PageId> CreateStory(
      fidl::VectorPtr<fuchsia::modular::StoryInfoExtraEntry> extra_info,
      bool is_kind_of_proto_story);

  // Deletes the |story_id| from the list of known stories and completes the
  // returned Future when done.
  //
  // Does not currently delete the story's page, so it is left dangling.
  //
  // TODO(thatguy): Deleting stories is a two-step process:
  //   1) Remove the story from the list of active stories (so it doesn't show
  //      up on the timeline).
  //   2) Delete the underlying story storage page.
  //
  // We only do (1). Find a way to split (1) and (2): either have the client
  // pass in a Future that signals when it's OK to delete the story storage (ie,
  // once the story has shut down cleanly) or split the function into two calls.
  // MI4-1002
  FuturePtr<> DeleteStory(fidl::StringPtr story_id);

  // Sets the last focused timestamp for |story_id| to |ts|. Completes the
  // returned Future when done.
  FuturePtr<> UpdateLastFocusedTimestamp(fidl::StringPtr story_id, int64_t ts);

  // Returns a Future StoryDataPtr for |story_id|. If |story_id| is not a valid
  // story, the returned StoryDataPtr will be null.
  FuturePtr<fuchsia::modular::internal::StoryDataPtr> GetStoryDataById(
      fidl::StringPtr story_id);

  // Returns a Future StoryDataPtr for story with |name|. See CreateStory() for
  // more information.
  FuturePtr<fuchsia::modular::internal::StoryDataPtr> GetStoryDataByName(
      fidl::StringPtr name);

  // Returns a Future vector of StoryData for all stories in this session.
  //
  // TODO(thatguy): If the return value grows large, an dispatcher stream would
  // be a more appropriate return value.
  FuturePtr<fidl::VectorPtr<fuchsia::modular::internal::StoryData>>
  GetAllStoryData();

  // Returns a Future that promotes the story with |story_id| to a regular
  // story.
  FuturePtr<> PromoteKindOfProtoStory(fidl::StringPtr story_id);

  // Deletes the story with |story_id| provided that it has not been promoted to
  // a regular story.
  FuturePtr<> DeleteKindOfProtoStory(fidl::StringPtr story_id);

  // Gets the StoryStorage for the story with the given |story_id| to perform
  // operations on the story such as adding modules, updating links, etc.
  FuturePtr<std::unique_ptr<StoryStorage>> GetStoryStorage(
      fidl::StringPtr story_id);

  // TODO(thatguy): Eliminate all users of this, and remove it. We want all
  // interfaces with the Ledger to be contained within *Storage classes.
  LedgerClient* ledger_client() const { return ledger_client_; }

 private:
  // |PageClient|
  void OnPageChange(const std::string& key, const std::string& value) override;

  // |PageClient|
  void OnPageDelete(const std::string& key) override;

  LedgerClient* const ledger_client_;
  OperationQueue operation_queue_;

  std::function<void(fidl::StringPtr story_id)> on_story_deleted_;
  std::function<void(fidl::StringPtr story_id,
                     fuchsia::modular::internal::StoryData story_data)>
      on_story_updated_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionStorage);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_STORAGE_SESSION_STORAGE_H_
