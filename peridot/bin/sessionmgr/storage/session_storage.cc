// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/storage/session_storage.h"

#include <lib/fidl/cpp/clone.h>
#include <unordered_set>
#include "fuchsia/ledger/cpp/fidl.h"
#include "src/lib/uuid/uuid.h"

#include "peridot/bin/sessionmgr/storage/constants_and_utils.h"
#include "peridot/bin/sessionmgr/storage/session_storage_xdr.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/ledger_client/operations.h"

namespace modular {

SessionStorage::SessionStorage(LedgerClient* ledger_client,
                               LedgerPageId page_id)
    : PageClient("SessionStorage", ledger_client, page_id, kStoryKeyPrefix),
      ledger_client_(ledger_client) {
  FXL_DCHECK(ledger_client_ != nullptr);
}

namespace {

// TODO(rosswang): replace with |std::string::starts_with| after C++20
bool StartsWith(const std::string& string, const std::string& prefix) {
  return string.compare(0, prefix.size(), prefix) == 0;
}

fidl::StringPtr StoryNameToStoryDataKey(fidl::StringPtr story_name) {
  // Not escaped, because only one component after the prefix.
  return kStoryDataKeyPrefix + story_name.get();
}

fidl::StringPtr StoryNameFromStoryDataKey(fidl::StringPtr key) {
  return key->substr(sizeof(kStoryDataKeyPrefix) - 1);
}

fidl::StringPtr StoryNameToStorySnapshotKey(fidl::StringPtr story_name) {
  // Not escaped, because only one component after the prefix.
  return kStorySnapshotKeyPrefix + story_name.get();
}

std::unique_ptr<OperationBase> MakeGetStoryDataCall(
    fuchsia::ledger::Page* const page, fidl::StringPtr story_name,
    fit::function<void(fuchsia::modular::internal::StoryDataPtr)> result_call) {
  return std::make_unique<ReadDataCall<fuchsia::modular::internal::StoryData>>(
      page, StoryNameToStoryDataKey(story_name), true /* not_found_is_ok */,
      XdrStoryData, std::move(result_call));
};

std::unique_ptr<OperationBase> MakeWriteStoryDataCall(
    fuchsia::ledger::Page* const page,
    fuchsia::modular::internal::StoryDataPtr story_data,
    fit::function<void()> result_call) {
  return std::make_unique<WriteDataCall<fuchsia::modular::internal::StoryData>>(
      page, StoryNameToStoryDataKey(story_data->story_info().id), XdrStoryData,
      std::move(story_data), std::move(result_call));
};

class CreateStoryCall
    : public LedgerOperation<fidl::StringPtr, fuchsia::ledger::PageId> {
 public:
  CreateStoryCall(
      fuchsia::ledger::Ledger* const ledger,
      fuchsia::ledger::Page* const root_page, fidl::StringPtr story_name,
      fidl::VectorPtr<fuchsia::modular::StoryInfoExtraEntry> extra_info,
      fuchsia::modular::StoryOptions story_options, ResultCall result_call)
      : LedgerOperation("SessionStorage::CreateStoryCall", ledger, root_page,
                        std::move(result_call)),
        story_name_(std::move(story_name)),
        extra_info_(std::move(extra_info)),
        story_options_(std::move(story_options)) {}

 private:
  void Run() override {
    FlowToken flow{this, &story_name_, &story_page_id_};
    ledger()->GetPage(nullptr, story_page_.NewRequest());
    story_page_->GetId([this, flow](fuchsia::ledger::PageId id) {
      story_page_id_ = std::move(id);
      Cont(flow);
    });
  }

  void Cont(FlowToken flow) {
    // TODO(security), cf. FW-174. This ID is exposed in public services
    // such as fuchsia::modular::StoryProvider.PreviousStories(),
    // fuchsia::modular::StoryController.GetInfo(),
    // fuchsia::modular::ModuleContext.GetStoryName(). We need to ensure this
    // doesn't expose internal information by being a page ID.
    // TODO(thatguy): Generate a GUID instead.
    if (!story_name_ || story_name_->empty()) {
      story_name_ = uuid::Generate();
    }

    story_data_ = fuchsia::modular::internal::StoryData::New();
    story_data_->set_story_name(story_name_);
    story_data_->set_story_options(std::move(story_options_));
    story_data_->set_story_page_id(std::move(story_page_id_));
    story_data_->mutable_story_info()->id = story_name_;
    story_data_->mutable_story_info()->last_focus_time = 0;
    story_data_->mutable_story_info()->extra = std::move(extra_info_);
    operation_queue_.Add(MakeWriteStoryDataCall(page(), std::move(story_data_),
                                                [this, flow] {}));
  }

  fidl::StringPtr story_name_;
  fidl::VectorPtr<fuchsia::modular::StoryInfoExtraEntry> extra_info_;
  fuchsia::modular::StoryOptions story_options_;

  fuchsia::ledger::PagePtr story_page_;
  fuchsia::modular::internal::StoryDataPtr story_data_;

  fuchsia::ledger::PageId story_page_id_;

  // Sub operations run in this queue.
  OperationQueue operation_queue_;
};

}  // namespace

FuturePtr<fidl::StringPtr, fuchsia::ledger::PageId> SessionStorage::CreateStory(
    fidl::StringPtr story_name,
    fidl::VectorPtr<fuchsia::modular::StoryInfoExtraEntry> extra_info,
    fuchsia::modular::StoryOptions story_options) {
  auto ret = Future<fidl::StringPtr, fuchsia::ledger::PageId>::Create(
      "SessionStorage.CreateStory.ret");
  operation_queue_.Add(std::make_unique<CreateStoryCall>(
      ledger_client_->ledger(), page(), std::move(story_name),
      std::move(extra_info), std::move(story_options), ret->Completer()));
  return ret;
}

FuturePtr<fidl::StringPtr, fuchsia::ledger::PageId> SessionStorage::CreateStory(
    fidl::VectorPtr<fuchsia::modular::StoryInfoExtraEntry> extra_info,
    fuchsia::modular::StoryOptions story_options) {
  return CreateStory(nullptr /* story_name */, std::move(extra_info),
                     std::move(story_options));
}

namespace {
class DeleteStoryCall : public Operation<> {
 public:
  DeleteStoryCall(fuchsia::ledger::Ledger* const ledger,
                  fuchsia::ledger::Page* const session_page,
                  fidl::StringPtr story_name, ResultCall result_call)
      : Operation("SessionStorage::DeleteStoryCall", std::move(result_call)),
        ledger_(ledger),
        session_page_(session_page),
        story_name_(story_name) {}

 private:
  void Run() override {
    FlowToken flow{this};
    operation_queue_.Add(MakeGetStoryDataCall(
        session_page_, story_name_, [this, flow](auto story_data) {
          if (!story_data)
            return;
          story_data_ = std::move(*story_data);
          Cont(flow);
        }));
  }

  void Cont(FlowToken flow) {
    // Get the story page so we can remove its contents.
    ledger_->GetPage(std::make_unique<fuchsia::ledger::PageId>(
                         *std::move(story_data_.mutable_story_page_id())),
                     story_page_.NewRequest());
    story_page_->ClearNew();
    // Remove the story data in the session page.
    session_page_->DeleteNew(
        to_array(StoryNameToStoryDataKey(story_data_.story_info().id)));
    // Remove the story snapshot in the session page.
    session_page_->DeleteNew(
        to_array(StoryNameToStorySnapshotKey(story_data_.story_info().id)));
  }

  fuchsia::ledger::Ledger* const ledger_;      // not owned
  fuchsia::ledger::Page* const session_page_;  // not owned
  const fidl::StringPtr story_name_;

  // Intermediate state.
  OperationQueue operation_queue_;
  fuchsia::modular::internal::StoryData story_data_;
  fuchsia::ledger::PagePtr story_page_;
};
}  // namespace

FuturePtr<> SessionStorage::DeleteStory(fidl::StringPtr story_name) {
  auto ret = Future<>::Create("SessionStorage.DeleteStory.ret");
  operation_queue_.Add(std::make_unique<DeleteStoryCall>(
      ledger_client_->ledger(), page(), story_name, ret->Completer()));
  return ret;
}

namespace {
class MutateStoryDataCall : public Operation<> {
 public:
  MutateStoryDataCall(
      fuchsia::ledger::Page* const page, fidl::StringPtr story_name,
      fit::function<bool(fuchsia::modular::internal::StoryData* story_data)>
          mutate,
      ResultCall result_call)
      : Operation("SessionStorage::MutateStoryDataCall",
                  std::move(result_call)),
        page_(page),
        story_name_(story_name),
        mutate_(std::move(mutate)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    operation_queue_.Add(MakeGetStoryDataCall(
        page_, story_name_,
        [this, flow](fuchsia::modular::internal::StoryDataPtr story_data) {
          if (!story_data) {
            // If the story doesn't exist, it was deleted.
            return;
          }
          if (!mutate_(story_data.get())) {
            // If no mutation happened, we're done.
            return;
          }

          operation_queue_.Add(
              MakeWriteStoryDataCall(page_, std::move(story_data), [flow] {}));
        }));
  }

  fuchsia::ledger::Page* const page_;  // not owned
  const fidl::StringPtr story_name_;
  fit::function<bool(fuchsia::modular::internal::StoryData* story_data)>
      mutate_;

  OperationQueue operation_queue_;
};

}  // namespace

FuturePtr<> SessionStorage::UpdateLastFocusedTimestamp(
    fidl::StringPtr story_name, const int64_t ts) {
  auto mutate = [ts](fuchsia::modular::internal::StoryData* const story_data) {
    if (story_data->story_info().last_focus_time == ts) {
      return false;
    }
    story_data->mutable_story_info()->last_focus_time = ts;
    return true;
  };

  auto ret = Future<>::Create("SessionStorage.UpdateLastFocusedTimestamp.ret");
  operation_queue_.Add(std::make_unique<MutateStoryDataCall>(
      page(), story_name, mutate, ret->Completer()));
  return ret;
}

FuturePtr<fuchsia::modular::internal::StoryDataPtr>
SessionStorage::GetStoryData(fidl::StringPtr story_name) {
  auto ret = Future<fuchsia::modular::internal::StoryDataPtr>::Create(
      "SessionStorage.GetStoryData.ret");
  operation_queue_.Add(
      MakeGetStoryDataCall(page(), story_name, ret->Completer()));
  return ret;
}

// Returns a Future vector of StoryData for all stories in this session.
FuturePtr<std::vector<fuchsia::modular::internal::StoryData>>
SessionStorage::GetAllStoryData() {
  auto ret = Future<std::vector<fuchsia::modular::internal::StoryData>>::Create(
      "SessionStorage.GetAllStoryData.ret");
  operation_queue_.Add(
      std::make_unique<ReadAllDataCall<fuchsia::modular::internal::StoryData>>(
          page(), kStoryDataKeyPrefix, XdrStoryData, ret->Completer()));
  return ret;
}

FuturePtr<> SessionStorage::UpdateStoryOptions(
    fidl::StringPtr story_name, fuchsia::modular::StoryOptions story_options) {
  auto ret = Future<>::Create("SessionStorage.SetOptions.ret");
  auto mutate = [story_options = std::move(story_options)](
                    fuchsia::modular::internal::StoryData* story_data) mutable {
    if (story_data->story_options() != story_options) {
      story_data->set_story_options(std::move(story_options));
      return true;
    }
    return false;
  };
  operation_queue_.Add(std::make_unique<MutateStoryDataCall>(
      page(), story_name, std::move(mutate), ret->Completer()));
  return ret;
}

FuturePtr<std::unique_ptr<StoryStorage>> SessionStorage::GetStoryStorage(
    fidl::StringPtr story_name) {
  auto returned_future = Future<std::unique_ptr<StoryStorage>>::Create(
      "SessionStorage.GetStoryStorage.returned_future");

  operation_queue_.Add(MakeGetStoryDataCall(
      page(), story_name,
      [this, returned_future,
       story_name](fuchsia::modular::internal::StoryDataPtr story_data) {
        if (story_data) {
          auto story_storage = std::make_unique<StoryStorage>(
              ledger_client_, story_data->story_page_id());
          returned_future->Complete(std::move(story_storage));
        } else {
          returned_future->Complete(nullptr);
        }
      }));

  return returned_future;
}

namespace {

class WriteSnapshotCall : public Operation<> {
 public:
  WriteSnapshotCall(PageClient* page_client, fidl::StringPtr story_name,
                    fuchsia::mem::Buffer snapshot, ResultCall result_call)
      : Operation("SessionStorage::WriteSnapshotCall", std::move(result_call)),
        page_client_(page_client),
        key_(StoryNameToStorySnapshotKey(story_name)),
        snapshot_(std::move(snapshot)) {}

 private:
  void Run() override {
    FlowToken flow{this};
    page_client_->page()->CreateReferenceFromBufferNew(
        std::move(snapshot_),
        [this, flow](fuchsia::ledger::CreateReferenceStatus status,
                     std::unique_ptr<fuchsia::ledger::Reference> reference) {
          if (status != fuchsia::ledger::CreateReferenceStatus::OK) {
            FXL_LOG(ERROR) << trace_name()
                           << " PageSnapshot.CreateReferenceFromBuffer() "
                           << fidl::ToUnderlying(status);
            return;
          } else if (!reference) {
            return;
          }

          PutReference(std::move(reference), flow);
        });
  }

  void PutReference(std::unique_ptr<fuchsia::ledger::Reference> reference,
                    FlowToken flow) {
    page_client_->page()->PutReferenceNew(
        to_array(key_), std::move(*reference),
        // TODO(MI4-1425): Experiment with declaring lazy priority.
        fuchsia::ledger::Priority::EAGER);
  }

  PageClient* const page_client_;
  fidl::StringPtr key_;
  fuchsia::mem::Buffer snapshot_;
};

class ReadSnapshotCall : public Operation<fuchsia::mem::BufferPtr> {
 public:
  ReadSnapshotCall(PageClient* page_client, fidl::StringPtr story_name,
                   ResultCall result_call)
      : Operation("SessionStorage::ReadSnapshotCall", std::move(result_call)),
        page_client_(page_client),
        key_(StoryNameToStorySnapshotKey(story_name)) {}

 private:
  void Run() override {
    FlowToken flow{this, &snapshot_};

    page_snapshot_ = page_client_->NewSnapshot(
        /* on_error = */ [this] { Done(nullptr); });
    page_snapshot_->Get(
        to_array(key_), [this, flow](fuchsia::ledger::Status status,
                                     fuchsia::mem::BufferPtr snapshot) {
          // TODO(MI4-1425): Handle NEEDS_FETCH status if using lazy priority.
          switch (status) {
            case fuchsia::ledger::Status::KEY_NOT_FOUND:
              return;
            case fuchsia::ledger::Status::OK:
              snapshot_ = std::move(snapshot);
              return;
            default:
              FXL_LOG(ERROR) << trace_name() << " PageSnapshot.Get() "
                             << fidl::ToUnderlying(status);
              return;
          }
        });
  }

  // Input parameters.
  PageClient* const page_client_;
  fidl::StringPtr key_;

  // Intermediate state.
  fuchsia::ledger::PageSnapshotPtr page_snapshot_;

  // Return values.
  fuchsia::mem::BufferPtr snapshot_;
};
}  // namespace

FuturePtr<> SessionStorage::WriteSnapshot(fidl::StringPtr story_name,
                                          fuchsia::mem::Buffer snapshot) {
  auto ret = Future<>::Create("SessionStorage.WriteSnapshot.ret");
  operation_queue_.Add(std::make_unique<WriteSnapshotCall>(
      this, story_name, std::move(snapshot), ret->Completer()));
  return ret;
}

FuturePtr<fuchsia::mem::BufferPtr> SessionStorage::ReadSnapshot(
    fidl::StringPtr story_name) {
  auto ret = Future<fuchsia::mem::BufferPtr>::Create(
      "SessionStorage.ReadSnapshot.ret");
  operation_queue_.Add(
      std::make_unique<ReadSnapshotCall>(this, story_name, ret->Completer()));
  return ret;
}

void SessionStorage::OnPageChange(const std::string& key,
                                  const std::string& value) {
  if (StartsWith(key, kStoryDataKeyPrefix)) {
    auto story_data = fuchsia::modular::internal::StoryData::New();
    if (!XdrRead(value, &story_data, XdrStoryData)) {
      FXL_LOG(ERROR)
          << "SessionStorage::OnPageChange : could not decode ledger "
             "value for key "
          << key << "\nvalue:\n"
          << value;
      return;
    }

    auto story_name = StoryNameFromStoryDataKey(key);
    if (on_story_updated_) {
      on_story_updated_(std::move(story_name), std::move(*story_data));
    }
  } else {
    // No-op.
  }
}

void SessionStorage::OnPageDelete(const std::string& key) {
  if (on_story_deleted_) {
    // Call to StoryNameFromStoryDataKey() needed because a deleted story is
    // modelled by deleting the key, and then the value is not available.
    // TODO(thatguy,mesch): Change PageClient to supply values of deleted keys
    // and/or change modeling of deleted stories.
    on_story_deleted_(StoryNameFromStoryDataKey(key));
  }
}

}  // namespace modular
