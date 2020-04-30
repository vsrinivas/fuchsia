// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_STORAGE_STORY_STORAGE_H_
#define SRC_MODULAR_BIN_SESSIONMGR_STORAGE_STORY_STORAGE_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fit/defer.h>

#include <map>

#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/ledger_client/ledger_client.h"
#include "src/modular/lib/ledger_client/page_client.h"
#include "src/modular/lib/ledger_client/page_id.h"

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

  enum class Status {
    OK = 0,
    LEDGER_ERROR = 1,
    VMO_COPY_ERROR = 2,
    // Indicates the storage operation detected either an invalid or conflicting
    // entity type (e.g. an empty type string or a write with a mismatched
    // type).
    INVALID_ENTITY_TYPE = 3,
    // Indicates the storage operation detected an invalid entity cookie (e.g.
    // an empty cookie).
    INVALID_ENTITY_COOKIE = 4,
  };

  // =========================================================================
  // ModuleData

  // Sets the callback that is called whenever ModuleData is added or updated
  // in underlying storage. Excludes notifications for changes (such as with
  // WriteModuleData() or UpdateModuleData()) made on this instance of
  // StoryStorage.
  void set_on_module_data_updated(fit::function<void(ModuleData)> callback) {
    on_module_data_updated_ = std::move(callback);
  }

  // Returns the current ModuleData for |module_path|. If not found, the
  // returned value is null.
  FuturePtr<ModuleDataPtr> ReadModuleData(const std::vector<std::string>& module_path);

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
  FuturePtr<> UpdateModuleData(const std::vector<std::string>& module_path,
                               fit::function<void(ModuleDataPtr*)> mutate_fn);

  // Returns all ModuleData entries for all mods.
  FuturePtr<std::vector<ModuleData>> ReadAllModuleData();

  // =========================================================================
  // Link data

  // Returns the value for |link_path|.
  //
  // The returned value will be stringified JSON. If no value is found, returns
  // "null", the JSON string for a null value.
  FuturePtr<Status, std::string> GetLinkValue(const LinkPath& link_path);

  // Fetches the link value at |link_path| and passes it to |mutate_fn|.
  // |mutate_fn| must synchronously update the StringPtr with the desired new
  // value for the link and return. The new value will be written to storage
  // and the returned future completed with the status.
  //
  // |mutate_fn|'s |value| points to the current value for the link and may be
  // modified. If the link is new and has no value, value->is_null() will be
  // true. Otherwise, *value will be valid JSON and must remain valid JSON
  // after |mutate_fn| is done.
  //
  // |context| is carried with the mutation operation and passed to any
  // notifications about this change on this instance of StoryStorage. A value
  // of nullptr for |context| is illegal.
  FuturePtr<Status> UpdateLinkValue(const LinkPath& link_path,
                                    fit::function<void(fidl::StringPtr* value)> mutate_fn,
                                    const void* context);

  // Completes the returned future after all prior methods have completed.
  FuturePtr<> Sync();

 private:
  // |PageClient|
  void OnPageChange(const std::string& key, fuchsia::mem::BufferPtr value) override;

  // |PageClient|
  void OnPageDelete(const std::string& key) override;

  // |PageClient|
  void OnPageConflict(Conflict* conflict) override;

  // Completes the returned Future when the ledger notifies us (through
  // OnPageChange()) of a write for |key| with |value|.
  FuturePtr<> WaitForWrite(const std::string& key, const std::string& value);

  fxl::WeakPtr<StoryStorage> GetWeakPtr();

  LedgerClient* const ledger_client_;
  const fuchsia::ledger::PageId page_id_;
  // NOTE: This operation queue serializes all link operations, even though
  // operations on different links do not have an impact on each other. Consider
  // adding an OperationQueue per link if we want to increase concurrency.
  OperationQueue operation_queue_;

  // Called when new ModuleData is encountered from the Ledger.
  fit::function<void(ModuleData)> on_module_data_updated_;

  // A map of ledger (key, value) to (vec of future). When we see a
  // notification in OnPageChange() for a matching (key, value), we complete
  // all the respective futures.
  //
  // NOTE: we use a map<> of vector<> here instead of a multimap<> because we
  // complete all the Futures for a given key/value pair at once.
  std::map<std::pair<std::string, std::string>, std::vector<FuturePtr<>>> pending_writes_;

  fxl::WeakPtrFactory<StoryStorage> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryStorage);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_STORAGE_STORY_STORAGE_H_
