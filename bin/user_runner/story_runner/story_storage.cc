// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/story_runner/story_storage.h"

#include <fuchsia/modular/internal/cpp/fidl.h>
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/user_runner/story_runner/story_storage_xdr.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/ledger_client/operations.h"
#include "peridot/lib/ledger_client/storage.h"

namespace modular {

StoryStorage::StoryStorage(LedgerClient* ledger_client,
                           fuchsia::ledger::PageId page_id)
    : PageClient("StoryStorage", ledger_client, page_id, kModuleKeyPrefix),
      ledger_client_(ledger_client),
      page_id_(page_id),
      weak_ptr_factory_(this) {
  FXL_DCHECK(ledger_client_ != nullptr);
}

FuturePtr<> StoryStorage::WriteModuleData(ModuleData module_data) {
  auto module_path = fidl::Clone(module_data.module_path);
  return UpdateModuleData(
      module_path, fxl::MakeCopyable([module_data = std::move(module_data)](
                                         ModuleDataPtr* module_data_ptr) {
        *module_data_ptr = ModuleData::New();
        module_data.Clone(module_data_ptr->get());
      }));
}

namespace {
struct UpdateModuleDataState {
  fidl::VectorPtr<fidl::StringPtr> module_path;
  std::function<void(ModuleDataPtr*)> mutate_fn;
  OperationQueue sub_operations;
};

}  // namespace

FuturePtr<> StoryStorage::UpdateModuleData(
    const fidl::VectorPtr<fidl::StringPtr>& module_path,
    std::function<void(ModuleDataPtr*)> mutate_fn) {
  auto op_state = std::make_shared<UpdateModuleDataState>();
  op_state->module_path = fidl::Clone(module_path);
  op_state->mutate_fn = std::move(mutate_fn);

  auto key = MakeModuleKey(module_path);
  auto op_body = [this, op_state, key](OperationBase* op) {
    auto did_read =
        Future<ModuleDataPtr>::Create("StoryStorage.UpdateModuleData.did_read");
    op_state->sub_operations.Add(
        new ReadDataCall<ModuleData>(page(), key, true /* not_found_is_ok */,
                                     XdrModuleData, did_read->Completer()));

    auto did_mutate = did_read->AsyncMap(
        [this, op_state, key](ModuleDataPtr current_module_data) {
          auto new_module_data = CloneOptional(current_module_data);
          op_state->mutate_fn(&new_module_data);

          if (!new_module_data && !current_module_data) {
            return Future<>::CreateCompleted(
                "StoryStorage.UpdateModuleData.did_mutate");
          }

          if (current_module_data) {
            FXL_DCHECK(new_module_data)
                << "StoryStorage::UpdateModuleData(): mutate_fn() must not "
                   "set to null an existing ModuleData record.";
          }
          FXL_DCHECK(new_module_data->module_path == op_state->module_path)
              << "StorageStorage::UpdateModuleData(path, ...): mutate_fn() "
                 "must set "
                 "ModuleData.module_path to |path|.";

          // We complete this Future chain when the Ledger gives us the
          // notification that |module_data| has been written. The Ledger
          // won't do that if the current value for |key| won't change, so
          // we have to short-circuit here.
          if (current_module_data && *current_module_data == *new_module_data) {
            return Future<>::CreateCompleted(
                "StoryStorage.UpdateModuleData.did_mutate");
          }

          auto module_data_copy = CloneOptional(new_module_data);
          std::string expected_value;
          XdrWrite(&expected_value, &module_data_copy, XdrModuleData);

          op_state->sub_operations.Add(new WriteDataCall<ModuleData>(
              page(), key, XdrModuleData, std::move(module_data_copy), [] {}));

          return WaitForWrite(key, expected_value);
        });

    return did_mutate;
  };

  auto ret = Future<>::Create("StoryStorage.ReadModuleData.ret");
  operation_queue_.Add(NewCallbackOperation(
      "StoryStorage::UpdateModuleData", std::move(op_body), ret->Completer()));
  return ret;
}

// Returns the current ModuleData for |module_path|.
FuturePtr<ModuleDataPtr> StoryStorage::ReadModuleData(
    const fidl::VectorPtr<fidl::StringPtr>& module_path) {
  auto key = MakeModuleKey(module_path);
  auto ret = Future<ModuleDataPtr>::Create("StoryStorage.ReadModuleData.ret");
  operation_queue_.Add(
      new ReadDataCall<ModuleData>(page(), key, true /* not_found_is_ok */,
                                   XdrModuleData, ret->Completer()));
  return ret;
}

// Returns all ModuleData entries for all mods.
FuturePtr<fidl::VectorPtr<ModuleData>> StoryStorage::ReadAllModuleData() {
  auto ret = Future<fidl::VectorPtr<ModuleData>>::Create(
      "StoryStorage.ReadAllModuleData.ret");
  operation_queue_.Add(new ReadAllDataCall<ModuleData>(
      page(), kModuleKeyPrefix, XdrModuleData, ret->Completer()));
  return ret;
}

void StoryStorage::OnPageChange(const std::string& key,
                                const std::string& value) {
  // If there are any operations waiting on this particular write
  // having happened, tell them to continue.
  auto it = pending_writes_.find(std::make_pair(key, value));
  if (it != pending_writes_.end()) {
    auto local_futures = std::move(it->second);
    for (auto fut : local_futures) {
      fut->Complete();
    }

    // Since the above write originated from this StoryStorage instance,
    // we do not notify any listeners.
    return;
  }

  // Notify our listener about the updated ModuleData.
  if (on_module_data_updated_) {
    auto module_data = ModuleData::New();
    if (!XdrRead(value, &module_data, XdrModuleData)) {
      FXL_LOG(ERROR) << "Unable to parse ModuleData " << key << " " << value;
      return;
    }
    on_module_data_updated_(std::move(*module_data));
  }
}

void StoryStorage::OnPageDelete(const std::string& key) {
  // ModuleData are never deleted, although it is theoretically possible that
  // conflict resolution results in a key disappearing. We do not currently do
  // this.
}

FuturePtr<> StoryStorage::WaitForWrite(const std::string& key,
                                       const std::string& value) {
  // TODO(thatguy): It is possible that through conflict resolution, the write
  // we expect to get will never arrive.  We must have the conflict resolver
  // update |pending_writes_| with the result of conflict resolution.
  auto did_see_write =
      Future<>::Create("StoryStorage.WaitForWrite.did_see_write");
  pending_writes_[std::make_pair(key, value)].push_back(did_see_write);
  return did_see_write;
}

fxl::WeakPtr<StoryStorage> StoryStorage::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

}  // namespace modular
