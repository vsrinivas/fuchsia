// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_STORAGE_STORY_STORAGE_H_
#define PERIDOT_BIN_USER_RUNNER_STORAGE_STORY_STORAGE_H_

#include <fuchsia/modular/cpp/fidl.h>
#include "lib/async/cpp/future.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/page_id.h"

using fuchsia::modular::LinkPath;
using fuchsia::modular::LinkPathPtr;
using fuchsia::modular::ModuleData;
using fuchsia::modular::ModuleDataPtr;

namespace modular {

// This class has the following responsibilities:
//
// * Manage the persistence of metadata about what mods are part of a single
//   story.
// * Manage the persistence of link values in a single story.
// * Observe the metadata and call clients back when changes initiated by other
//   Ledger clients appear.
//
// All calls operate directly on the Ledger itself: no local caching is
// performed.
class StoryStorage : public PageClient {
 public:
  // Constructs a new StoryStorage with storage on |page_id| in the ledger
  // given by |ledger_client|.
  //
  // |ledger_client| must outlive *this.
  StoryStorage(LedgerClient* ledger_client, fuchsia::ledger::PageId page_id);

  enum Status { OK = 0 };

  // =========================================================================
  // ModuleData

  // Sets the callback that is called whenever ModuleData is added or updated
  // in underlying storage. Excludes notifications for changes (such as with
  // WriteModuleData() or UpdateModuleData()) made on this instance of
  // StoryStorage.
  void set_on_module_data_updated(std::function<void(ModuleData)> callback) {
    on_module_data_updated_ = std::move(callback);
  }

  // Returns the current ModuleData for |module_path|. If not found, the
  // returned value is null.
  FuturePtr<ModuleDataPtr> ReadModuleData(
      const fidl::VectorPtr<fidl::StringPtr>& module_path);

  // Writes |module_data| to storage. The returned future is completed
  // once |module_data| has been written and a notification confirming the
  // write has been received.
  FuturePtr<> WriteModuleData(ModuleData module_data);

  // Reads the ModuleData for |module_path|, calls |mutate_fn| which may modify
  // the contents, and writes the resulting ModuleData back to storage.
  // Completes the returned future once a notification confirming the write has
  // been received.
  //
  // If there is no ModuleData for |module_path|, |mutate_fn| will be called
  // with a null ModuleDataPtr. |mutate_fn| may initialize the ModuleDataPtr,
  // in which case a new ModuleData record will be written.
  //
  // It is illegal to change ModuleDataPtr->module_path in |mutate_fn| or to
  // reset to null an otherwise initialized ModuleDataPtr.
  FuturePtr<> UpdateModuleData(
      const fidl::VectorPtr<fidl::StringPtr>& module_path,
      std::function<void(ModuleDataPtr*)> mutate_fn);

  // Returns all ModuleData entries for all mods.
  FuturePtr<fidl::VectorPtr<ModuleData>> ReadAllModuleData();

  // =========================================================================
  // Link data

  // Use with AddLinkUpdatedCallback below.
  //
  // Called whenever a change occurs to the link(s) specified in
  // AddLinkUpdatedCallback(). The receiver gets the LinkPath, the current
  // |value| and whatever |context| was passed into the mutation call.
  //
  // The receiver should return true to continue receiving notifications
  // for the same links, or false to de-register the callback.
  using LinkUpdatedCallback = std::function<bool(
      const LinkPath&, const fidl::StringPtr& value, void* context)>;

  // Registers |callback| to be invoked whenever a change to the link value at
  // |link_path| occurs.  If |link_path| is null, watches all links.
  void AddLinkUpdatedCallback(LinkPathPtr link_path,
                              LinkUpdatedCallback callback);

  // Returns the value for |link_path| at |json_path|
  // (https://tools.ietf.org/html/rfc6901) within the JSON value.  A
  // |json_path| of "" or nullptr will return the entire value.
  //
  // The returned value will be stringified JSON. If no value is found, returns
  // a null StringPtr.
  FuturePtr<fidl::StringPtr> GetLinkValue(
      const LinkPath& link_path, fidl::VectorPtr<fidl::StringPtr> json_path);

  // Sets the value for |link_path| at |json_path| to the JSON encoded
  // |json_value|.
  //
  // |context| is carried with the mutation operation and passed to any
  // notifications about this change on this instance of StoryStorage.
  FuturePtr<Status, fidl::StringPtr> SetLinkValue(
      const LinkPath& link_path, fidl::VectorPtr<fidl::StringPtr> json_path,
      fidl::StringPtr json_value, void* context);

  // Like SetLinkValue(), but does not completely overwrite the JSON at
  // |json_path|.
  //
  // Recursively, for every attribute in |json_object|, sets or overwrites the
  // same attribute at |json_path| with the corresponding value in
  // |json_object|.
  //
  // |context| is carried with the mutation operation and passed to any
  // notifications about this change on this instance of StoryStorage.
  FuturePtr<Status, fidl::StringPtr> UpdateLinkObject(
      const LinkPath& link_path, fidl::VectorPtr<fidl::StringPtr> json_path,
      fidl::StringPtr json_object, void* context);

  // Erases the JSON value for |link_path| at |json_path|.
  //
  // |context| is carried with the mutation operation and passed to any
  // notifications about this change on this instance of StoryStorage.
  FuturePtr<Status, fidl::StringPtr> EraseLinkValue(
      const LinkPath& link_path, fidl::VectorPtr<fidl::StringPtr> json_path,
      void* context);

  // TODO(thatguy): Remove users of these and remove. Only used when
  // constructing a LinkImpl in StoryControllerImpl. Bring Link storage
  // into this class.
  LedgerClient* ledger_client() const { return ledger_client_; }
  fuchsia::ledger::PageId page_id() const { return page_id_; }

 private:
  // |PageClient|
  void OnPageChange(const std::string& key, const std::string& value) override;

  // |PageClient|
  void OnPageDelete(const std::string& key) override;

  // Completes the returned Future when the ledger notifies us (through
  // OnPageChange()) of a write for |key| with |value|.
  FuturePtr<> WaitForWrite(const std::string& key, const std::string& value);

  fxl::WeakPtr<StoryStorage> GetWeakPtr();

  LedgerClient* const ledger_client_;
  const fuchsia::ledger::PageId page_id_;
  OperationQueue operation_queue_;

  std::function<void(ModuleData)> on_module_data_updated_;

  // A map of ledger (key, value) to (vec of future). When we see a
  // notification in OnPageChange() for a matching (key, value), we complete
  // all the respective futures.
  std::map<std::pair<std::string, std::string>, std::vector<FuturePtr<>>>
      pending_writes_;

  fxl::WeakPtrFactory<StoryStorage> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryStorage);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_STORAGE_STORY_STORAGE_H_
