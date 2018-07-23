// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/storage/story_storage.h"

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fxl/functional/make_copyable.h>

#include "peridot/bin/user_runner/storage/constants_and_utils.h"
#include "peridot/bin/user_runner/storage/story_storage_xdr.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/ledger_client/operations.h"

namespace modular {

StoryStorage::StoryStorage(LedgerClient* ledger_client,
                           fuchsia::ledger::PageId page_id)
    : PageClient("StoryStorage", ledger_client, page_id, "" /* key_prefix */),
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

  auto ret = Future<>::Create("StoryStorage.UpdateModuleData.ret");
  operation_queue_.Add(NewCallbackOperation(
      "StoryStorage::UpdateModuleData", std::move(op_body), ret->Completer()));
  return ret;
}

FuturePtr<ModuleDataPtr> StoryStorage::ReadModuleData(
    const fidl::VectorPtr<fidl::StringPtr>& module_path) {
  auto key = MakeModuleKey(module_path);
  auto ret = Future<ModuleDataPtr>::Create("StoryStorage.ReadModuleData.ret");
  operation_queue_.Add(
      new ReadDataCall<ModuleData>(page(), key, true /* not_found_is_ok */,
                                   XdrModuleData, ret->Completer()));
  return ret;
}

FuturePtr<fidl::VectorPtr<ModuleData>> StoryStorage::ReadAllModuleData() {
  auto ret = Future<fidl::VectorPtr<ModuleData>>::Create(
      "StoryStorage.ReadAllModuleData.ret");
  operation_queue_.Add(new ReadAllDataCall<ModuleData>(
      page(), kModuleKeyPrefix, XdrModuleData, ret->Completer()));
  return ret;
}

StoryStorage::LinkWatcherAutoCancel StoryStorage::WatchLink(
    const LinkPath& link_path, LinkUpdatedCallback callback) {
  auto it = link_watchers_.emplace(MakeLinkKey(link_path), std::move(callback));

  auto auto_remove = [weak_this = GetWeakPtr(), it] {
    if (!weak_this)
      return;

    weak_this->link_watchers_.erase(it);
  };

  return LinkWatcherAutoCancel(std::move(auto_remove));
}

namespace {
constexpr char kJsonNull[] = "null";

class ReadLinkDataCall
    : public Operation<StoryStorage::Status, fidl::StringPtr> {
 public:
  ReadLinkDataCall(PageClient* page_client, fidl::StringPtr key,
                   ResultCall result_call)
      : Operation("StoryStorage::ReadLinkDataCall", std::move(result_call)),
        page_client_(page_client),
        key_(std::move(key)) {}

 private:
  void Run() override {
    FlowToken flow{this, &status_, &value_};
    status_ = StoryStorage::Status::OK;

    page_snapshot_ = page_client_->NewSnapshot([this, weak_ptr = GetWeakPtr()] {
      if (!weak_ptr) {
        return;
      }
      // An error occurred getting the snapshot. Resetting page_snapshot_
      // will ensure that the FlowToken it has captured below while waiting for
      // a connected channel will be destroyed, and the operation will be
      // complete.
      status_ = StoryStorage::Status::LEDGER_ERROR;
      page_snapshot_ = fuchsia::ledger::PageSnapshotPtr();
    });

    page_snapshot_->Get(
        to_array(key_), [this, flow](fuchsia::ledger::Status status,
                                     fuchsia::mem::BufferPtr value) {
          std::string value_as_string;
          switch (status) {
            case fuchsia::ledger::Status::KEY_NOT_FOUND:
              // Leave value_ as a null-initialized StringPtr.
              return;
            case fuchsia::ledger::Status::OK:
              if (!value) {
                value_ = kJsonNull;
                return;
              }

              if (!fsl::StringFromVmo(*value, &value_as_string)) {
                FXL_LOG(ERROR) << trace_name() << " VMO could not be copied.";
                status_ = StoryStorage::Status::VMO_COPY_ERROR;
                return;
              }
              value_ = value_as_string;
              return;
            default:
              FXL_LOG(ERROR) << trace_name() << " PageSnapshot.Get() "
                             << fidl::ToUnderlying(status);
              status_ = StoryStorage::Status::LEDGER_ERROR;
              return;
          }
        });
  }

  // Input parameters.
  PageClient* const page_client_;
  const fidl::StringPtr key_;

  // Intermediate state.
  fuchsia::ledger::PageSnapshotPtr page_snapshot_;

  // Return values.
  StoryStorage::Status status_;
  fidl::StringPtr value_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ReadLinkDataCall);
};
}  // namespace

FuturePtr<StoryStorage::Status, fidl::StringPtr> StoryStorage::GetLinkValue(
    const LinkPath& link_path) {
  auto key = MakeLinkKey(link_path);
  auto ret = Future<Status, fidl::StringPtr>::Create(
      "StoryStorage::GetLinkValue " + key);
  operation_queue_.Add(new ReadLinkDataCall(this, key, ret->Completer()));
  // We use AsyncMap here, even though we could semantically use Map, because
  // we need to return >1 value and only AsyncMap lets you do that.
  return ret->AsyncMap([](StoryStorage::Status status, fidl::StringPtr value) {
    if (value.is_null()) {
      value = kJsonNull;
    }
    return Future<StoryStorage::Status, fidl::StringPtr>::CreateCompleted(
        "StoryStorage.GetLinkValue.AsyncMap", std::move(status),
        std::move(value));
  });
}

namespace {

class WriteLinkDataCall : public Operation<StoryStorage::Status> {
 public:
  WriteLinkDataCall(PageClient* page_client, fidl::StringPtr key,
                    fidl::StringPtr value, ResultCall result_call)
      : Operation("StoryStorage::WriteLinkDataCall", std::move(result_call)),
        page_client_(page_client),
        key_(std::move(key)),
        value_(std::move(value)) {}

 private:
  void Run() override {
    FlowToken flow{this, &status_};
    status_ = StoryStorage::Status::OK;

    page_client_->page()->Put(
        to_array(key_), to_array(value_),
        [this, flow, weak_ptr = GetWeakPtr()](fuchsia::ledger::Status status) {
          if (!weak_ptr) {
            return;
          }
          if (status != fuchsia::ledger::Status::OK) {
            FXL_LOG(ERROR) << "StoryStorage.WriteLinkDataCall " << key_ << " "
                           << " Page.Put() " << fidl::ToUnderlying(status);
            status_ = StoryStorage::Status::LEDGER_ERROR;
          }
        });
  }

  PageClient* const page_client_;
  fidl::StringPtr key_;
  fidl::StringPtr value_;

  StoryStorage::Status status_;

  FXL_DISALLOW_COPY_AND_ASSIGN(WriteLinkDataCall);
};

// Returns the status of the mutation and the new value. If no mutation
// happened, returns Status::OK and a nullptr.
class UpdateLinkCall : public Operation<StoryStorage::Status, fidl::StringPtr> {
 public:
  UpdateLinkCall(
      PageClient* page_client, std::string key,
      std::function<void(fidl::StringPtr*)> mutate_fn,
      std::function<FuturePtr<>(const std::string&, const std::string&)>
          wait_for_write_fn,
      ResultCall done)
      : Operation("StoryStorage::UpdateLinkCall", std::move(done)),
        page_client_(page_client),
        key_(std::move(key)),
        mutate_fn_(std::move(mutate_fn)),
        wait_for_write_fn_(std::move(wait_for_write_fn)) {}

 private:
  void Run() {
    FlowToken flow{this, &status_, &new_value_};

    operation_queue_.Add(
        new ReadLinkDataCall(page_client_, key_,
                             [this, flow](StoryStorage::Status status,
                                          fidl::StringPtr current_value) {
                               status_ = status;
                               if (status != StoryStorage::Status::OK) {
                                 return;
                               }

                               Mutate(flow, std::move(current_value));
                             }));
  }

  void Mutate(FlowToken flow, fidl::StringPtr current_value) {
    new_value_ = current_value;
    mutate_fn_(&new_value_);
    rapidjson::Document doc;
    doc.Parse(new_value_);
    if (new_value_.is_null() || doc.HasParseError()) {
      if (!new_value_.is_null()) {
        FXL_LOG(ERROR) << "StoryStorage.UpdateLinkCall.Mutate " << key_
                       << " invalid json: " << doc.GetParseError();
      }
      status_ = StoryStorage::Status::LINK_INVALID_JSON;
      return;
    }

    if (new_value_ == current_value) {
      // Set the returned new value to null so the caller knows we succeeded
      // but didn't write anything.
      new_value_ = nullptr;
      return;
    }

    operation_queue_.Add(new WriteLinkDataCall(
        page_client_, key_, new_value_,
        [this, flow](StoryStorage::Status status) {
          status_ = status;

          // If we succeeded AND we set a new value, we need to wait for
          // confirmation from the ledger.
          if (status == StoryStorage::Status::OK && new_value_) {
            wait_for_write_fn_(key_, new_value_)->Then([this, flow] {
              Done(std::move(status_), std::move(new_value_));
            });
          }
        }));
  }

  // Input parameters.
  PageClient* const page_client_;
  const std::string key_;
  std::function<void(fidl::StringPtr*)> mutate_fn_;
  std::function<FuturePtr<>(const std::string&, const std::string&)>
      wait_for_write_fn_;

  // Operation runtime state.
  OperationQueue operation_queue_;

  // Return values.
  StoryStorage::Status status_;
  fidl::StringPtr new_value_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UpdateLinkCall);
};

}  // namespace

FuturePtr<StoryStorage::Status> StoryStorage::UpdateLinkValue(
    const LinkPath& link_path, std::function<void(fidl::StringPtr*)> mutate_fn,
    const void* context) {
  // nullptr is reserved for updates that came from other instances of
  // StoryStorage.
  FXL_DCHECK(context != nullptr)
      << "StoryStorage::UpdateLinkValue(..., context) of nullptr is reserved.";

  auto key = MakeLinkKey(link_path);
  auto did_update = Future<Status, fidl::StringPtr>::Create(
      "StoryStorage.UpdateLinkValue.did_update");
  operation_queue_.Add(new UpdateLinkCall(
      this, key, std::move(mutate_fn),
      std::bind(&StoryStorage::WaitForWrite, this, std::placeholders::_1,
                std::placeholders::_2),
      did_update->Completer()));

  // We can't chain this call to the parent future chain because we do
  // not want it to happen at all in the case of errors.
  return did_update->WeakMap(
      GetWeakPtr(), [this, key, context](StoryStorage::Status status,
                                         fidl::StringPtr new_value) {
        // if |new_value| is null, it means we didn't write any new data, even
        // if |status| == OK.
        if (status == StoryStorage::Status::OK && !new_value.is_null()) {
          NotifyLinkWatchers(key, new_value, context);
        }

        return status;
      });
}

FuturePtr<> StoryStorage::Sync() {
  auto ret = Future<>::Create("StoryStorage::Sync.ret");
  operation_queue_.Add(NewCallbackOperation("StoryStorage::Sync",
                                            [](OperationBase* op) {
                                              return Future<>::CreateCompleted(
                                                  "StoryStorage::Sync");
                                            },
                                            ret->Completer()));
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

  if (key.find(kLinkKeyPrefix) == 0) {
    NotifyLinkWatchers(key, value, nullptr /* context */);
  } else if (key.find(kModuleKeyPrefix) == 0) {
    if (on_module_data_updated_) {
      auto module_data = ModuleData::New();
      if (!XdrRead(value, &module_data, XdrModuleData)) {
        FXL_LOG(ERROR) << "Unable to parse ModuleData " << key << " " << value;
        return;
      }
      on_module_data_updated_(std::move(*module_data));
    }
  } else {
    // TODO(thatguy): We store some Link data on the root page (where StoryData
    // is stored) for the user shell to make use of. This means we get notified
    // in that instance of changes we don't care about.
    //
    // Consider putting all story-scoped data under a shared prefix, and use
    // that when initializing the PageClient.
    FXL_LOG(ERROR) << "Unexpected StoryStorage Ledger key prefix: " << key;
  }
}

void StoryStorage::OnPageDelete(const std::string& key) {
  // ModuleData and Link values are never deleted, although it is
  // theoretically possible that conflict resolution results in a key
  // disappearing. We do not currently do this.
}

void StoryStorage::OnPageConflict(Conflict* conflict) {
  // TODO(thatguy): Add basic conflict resolution. We can force a conflict for
  // link data in tests by using Page.StartTranscation() in UpdateLinkValue().
  FXL_LOG(WARNING) << "StoryStorage::OnPageConflict() for link key "
                   << to_string(conflict->key);
}

void StoryStorage::NotifyLinkWatchers(const std::string& link_key,
                                      fidl::StringPtr value,
                                      const void* context) {
  auto range = link_watchers_.equal_range(link_key);
  for (auto it = range.first; it != range.second; ++it) {
    it->second(value, context);
  }
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
