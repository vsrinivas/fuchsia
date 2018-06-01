// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_STORY_RUNNER_STORY_STORAGE_H_
#define PERIDOT_BIN_USER_RUNNER_STORY_RUNNER_STORY_STORAGE_H_

#include <fuchsia/modular/cpp/fidl.h>
#include "lib/async/cpp/future.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/page_id.h"

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

  // Sets the callback that is called whenever ModuleData is added or updated
  // in underlying storage. Excludes notifications for changes (such as with
  // WriteModuleData() or UpdateModuleData()) made on this instance of
  // StoryStorage.
  void set_on_module_data_updated(
      std::function<void(fuchsia::modular::ModuleData)> callback) {
    on_module_data_updated_ = std::move(callback);
  }

  // Returns the current ModuleData for |module_path|. If not found, the
  // returned value is null.
  FuturePtr<fuchsia::modular::ModuleDataPtr> ReadModuleData(
      const fidl::VectorPtr<fidl::StringPtr>& module_path);

  // Writes |module_data| to storage. The returned future is completed
  // once |module_data| has been written and a notification confirming the
  // write has been received.
  FuturePtr<> WriteModuleData(fuchsia::modular::ModuleData module_data);

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
      std::function<void(fuchsia::modular::ModuleDataPtr*)> mutate_fn);

  // Returns all ModuleData entries for all mods.
  FuturePtr<fidl::VectorPtr<fuchsia::modular::ModuleData>> ReadAllModuleData();

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

  std::function<void(fuchsia::modular::ModuleData)> on_module_data_updated_;

  // A map of ledger (key, value) to (vec of future). When we see a
  // notification in OnPageChange() for a matching (key, value), we complete
  // all the respective futures.
  std::map<std::pair<std::string, std::string>, std::vector<FuturePtr<>>>
      pending_writes_;

  fxl::WeakPtrFactory<StoryStorage> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryStorage);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_STORY_RUNNER_STORY_STORAGE_H_
