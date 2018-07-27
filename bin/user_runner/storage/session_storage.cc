// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/storage/session_storage.h"

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fxl/functional/make_copyable.h>

#include "peridot/bin/user_runner/storage/constants_and_utils.h"
#include "peridot/bin/user_runner/storage/session_storage_xdr.h"
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

fidl::StringPtr StoryIdToLedgerKey(fidl::StringPtr id) {
  // Not escaped, because only one component after the prefix.
  return kStoryKeyPrefix + id.get();
}

fidl::StringPtr StoryIdFromLedgerKey(fidl::StringPtr key) {
  return key->substr(sizeof(kStoryKeyPrefix) - 1);
}

OperationBase* MakeGetStoryDataCall(
    fuchsia::ledger::Page* const page, fidl::StringPtr story_id,
    std::function<void(fuchsia::modular::internal::StoryDataPtr)> result_call) {
  return new ReadDataCall<fuchsia::modular::internal::StoryData>(
      page, StoryIdToLedgerKey(story_id), true /* not_found_is_ok */,
      XdrStoryData, std::move(result_call));
};

OperationBase* MakeWriteStoryDataCall(
    fuchsia::ledger::Page* const page,
    fuchsia::modular::internal::StoryDataPtr story_data,
    std::function<void()> result_call) {
  return new WriteDataCall<fuchsia::modular::internal::StoryData>(
      page, StoryIdToLedgerKey(story_data->story_info.id), XdrStoryData,
      std::move(story_data), std::move(result_call));
};

class CreateStoryCall
    : public LedgerOperation<fidl::StringPtr, fuchsia::ledger::PageId> {
 public:
  CreateStoryCall(
      fuchsia::ledger::Ledger* const ledger,
      fuchsia::ledger::Page* const root_page, fidl::StringPtr story_name,
      fidl::VectorPtr<fuchsia::modular::StoryInfoExtraEntry> extra_info,
      bool is_kind_of_proto_story, ResultCall result_call)
      : LedgerOperation("SessionStorage::CreateStoryCall", ledger, root_page,
                        std::move(result_call)),
        story_name_(std::move(story_name)),
        extra_info_(std::move(extra_info)),
        is_kind_of_proto_story_(is_kind_of_proto_story) {}

 private:
  void Run() override {
    FlowToken flow{this, &story_id_, &story_page_id_};
    ledger()->GetPage(nullptr, story_page_.NewRequest(),
                      Protect([this, flow](fuchsia::ledger::Status status) {
                        if (status != fuchsia::ledger::Status::OK) {
                          FXL_LOG(ERROR) << trace_name() << " "
                                         << "Ledger.GetPage() "
                                         << fidl::ToUnderlying(status);
                        }
                      }));
    story_page_->GetId([this, flow](fuchsia::ledger::PageId id) {
      story_page_id_ = std::move(id);
      Cont(flow);
    });
  }

  void Cont(FlowToken flow) {
    // TODO(security), cf. FW-174. This ID is exposed in public services
    // such as fuchsia::modular::StoryProvider.PreviousStories(),
    // fuchsia::modular::StoryController.GetInfo(),
    // fuchsia::modular::ModuleContext.GetStoryId(). We need to ensure this
    // doesn't expose internal information by being a page ID.
    // TODO(thatguy): Generate a GUID instead.
    story_id_ = to_hex_string(story_page_id_.id);

    story_data_ = fuchsia::modular::internal::StoryData::New();
    story_data_->story_name = story_name_;
    story_data_->is_kind_of_proto_story = is_kind_of_proto_story_;
    story_data_->story_page_id = CloneOptional(story_page_id_);
    story_data_->story_info.id = story_id_;
    story_data_->story_info.last_focus_time = 0;
    story_data_->story_info.extra = std::move(extra_info_);

    operation_queue_.Add(MakeWriteStoryDataCall(page(), std::move(story_data_),
                                                [this, flow] {}));
  }

  const fidl::StringPtr story_name_;
  fidl::VectorPtr<fuchsia::modular::StoryInfoExtraEntry> extra_info_;
  const bool is_kind_of_proto_story_;

  fuchsia::ledger::PagePtr story_page_;
  fuchsia::modular::internal::StoryDataPtr story_data_;

  fuchsia::ledger::PageId story_page_id_;
  fidl::StringPtr story_id_;  // This is the result of the Operation.

  // Sub operations run in this queue.
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CreateStoryCall);
};

}  // namespace

FuturePtr<fidl::StringPtr, fuchsia::ledger::PageId> SessionStorage::CreateStory(
    fidl::StringPtr story_name,
    fidl::VectorPtr<fuchsia::modular::StoryInfoExtraEntry> extra_info,
    bool is_kind_of_proto_story) {
  auto ret = Future<fidl::StringPtr, fuchsia::ledger::PageId>::Create(
      "SessionStorage.CreateStory.ret");
  operation_queue_.Add(new CreateStoryCall(
      ledger_client_->ledger(), page(), std::move(story_name),
      std::move(extra_info), is_kind_of_proto_story, ret->Completer()));
  return ret;
}

FuturePtr<fidl::StringPtr, fuchsia::ledger::PageId> SessionStorage::CreateStory(
    fidl::VectorPtr<fuchsia::modular::StoryInfoExtraEntry> extra_info,
    bool is_kind_of_proto_story) {
  return CreateStory(nullptr /* story_name */, std::move(extra_info),
                     is_kind_of_proto_story);
}

FuturePtr<> SessionStorage::DeleteStory(fidl::StringPtr story_id) {
  auto ret = Future<>::Create("SessionStorage.DeleteStory.ret");
  operation_queue_.Add(NewCallbackOperation(
      "SessionStorage::DeleteStory",
      [this, story_id](OperationBase* op) {
        auto deleted = Future<>::Create("SessionStorage.DeleteStory.deleted");
        page()->Delete(to_array(StoryIdToLedgerKey(story_id)),
                       [this, deleted](fuchsia::ledger::Status status) {
                         // Deleting a key that doesn't exist is OK, not
                         // KEY_NOT_FOUND.
                         if (status != fuchsia::ledger::Status::OK) {
                           FXL_LOG(ERROR) << "SessionStorage: Page.Delete() "
                                          << fidl::ToUnderlying(status);
                         }
                         deleted->Complete();
                       });
        return deleted;
      },
      ret->Completer()));
  return ret;
}

namespace {
class MutateStoryDataCall : public Operation<> {
 public:
  MutateStoryDataCall(
      fuchsia::ledger::Page* const page, fidl::StringPtr story_id,
      std::function<bool(fuchsia::modular::internal::StoryData* story_data)>
          mutate,
      ResultCall result_call)
      : Operation("SessionStorage::MutateStoryDataCall",
                  std::move(result_call)),
        page_(page),
        story_id_(story_id),
        mutate_(std::move(mutate)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    operation_queue_.Add(MakeGetStoryDataCall(
        page_, story_id_,
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
  const fidl::StringPtr story_id_;
  std::function<bool(fuchsia::modular::internal::StoryData* story_data)>
      mutate_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MutateStoryDataCall);
};

}  // namespace

FuturePtr<> SessionStorage::UpdateLastFocusedTimestamp(fidl::StringPtr story_id,
                                                       const int64_t ts) {
  auto mutate = [ts](fuchsia::modular::internal::StoryData* const story_data) {
    if (story_data->story_info.last_focus_time == ts) {
      return false;
    }
    story_data->story_info.last_focus_time = ts;
    return true;
  };

  auto ret = Future<>::Create("SessionStorage.UpdateLastFocusedTimestamp.ret");
  operation_queue_.Add(
      new MutateStoryDataCall(page(), story_id, mutate, ret->Completer()));
  return ret;
}

FuturePtr<fuchsia::modular::internal::StoryDataPtr>
SessionStorage::GetStoryDataById(fidl::StringPtr story_id) {
  auto ret = Future<fuchsia::modular::internal::StoryDataPtr>::Create(
      "SessionStorage.GetStoryDataById.ret");
  operation_queue_.Add(
      MakeGetStoryDataCall(page(), story_id, ret->Completer()));
  return ret;
}

FuturePtr<fuchsia::modular::internal::StoryDataPtr>
SessionStorage::GetStoryDataByName(fidl::StringPtr story_name) {
  auto ret = Future<fuchsia::modular::internal::StoryDataPtr>::Create(
      "SessionStorage.GetStoryDataByName.ret");
  // TODO(thatguy): This is inefficient. We should store a separate index of
  // story_name->story_id and perform a lookup.
  GetAllStoryData()->Then([ret, story_name](auto all_data) {
    for (auto& entry : *all_data) {
      if (entry.story_name == story_name) {
        ret->Complete(CloneOptional(entry));
        return;
      }
    }
    ret->Complete(nullptr);
  });
  return ret;
}

// Returns a Future vector of StoryData for all stories in this session.
FuturePtr<fidl::VectorPtr<fuchsia::modular::internal::StoryData>>
SessionStorage::GetAllStoryData() {
  auto ret =
      Future<fidl::VectorPtr<fuchsia::modular::internal::StoryData>>::Create(
          "SessionStorage.GetAllStoryData.ret");
  operation_queue_.Add(
      new ReadAllDataCall<fuchsia::modular::internal::StoryData>(
          page(), kStoryKeyPrefix, XdrStoryData, ret->Completer()));
  return ret;
}

FuturePtr<> SessionStorage::PromoteKindOfProtoStory(fidl::StringPtr story_id) {
  auto mutate = [](fuchsia::modular::internal::StoryData* const story_data) {
    if (story_data->is_kind_of_proto_story) {
      story_data->is_kind_of_proto_story = false;
      return true;
    }
    return false;
  };
  auto ret = Future<>::Create("SessionStorage.PromoteKindOfProtoStory.ret");
  operation_queue_.Add(
      new MutateStoryDataCall(page(), story_id, mutate, ret->Completer()));
  return ret;
}

FuturePtr<> SessionStorage::DeleteKindOfProtoStory(fidl::StringPtr story_id) {
  auto returned_future =
      Future<>::Create("SessionStorage.DeleteKindOfProtoStory.returned_future");

  operation_queue_.Add(MakeGetStoryDataCall(
      page(), story_id,
      [this, returned_future,
       story_id](fuchsia::modular::internal::StoryDataPtr story_data) {
        if (story_data && story_data->is_kind_of_proto_story) {
          page()->Delete(
              to_array(StoryIdToLedgerKey(story_id)),
              [this, returned_future](fuchsia::ledger::Status status) {
                // Deleting a key that doesn't exist is OK, not
                // KEY_NOT_FOUND.
                if (status != fuchsia::ledger::Status::OK) {
                  FXL_LOG(ERROR) << "SessionStorage: Page.Delete() "
                                 << fidl::ToUnderlying(status);
                }
                returned_future->Complete();
              });
        } else {
          returned_future->Complete();
        }
      }));

  return returned_future;
}

FuturePtr<std::unique_ptr<StoryStorage>> SessionStorage::GetStoryStorage(
    fidl::StringPtr story_id) {
  auto returned_future = Future<std::unique_ptr<StoryStorage>>::Create(
      "SessionStorage.GetStoryStorage.returned_future");

  operation_queue_.Add(MakeGetStoryDataCall(
      page(), story_id,
      [this, returned_future,
       story_id](fuchsia::modular::internal::StoryDataPtr story_data) {
        if (story_data) {
          auto story_storage = std::make_unique<StoryStorage>(
              ledger_client(), *story_data->story_page_id);
          returned_future->Complete(std::move(story_storage));
        } else {
          returned_future->Complete(nullptr);
        }
      }));

  return returned_future;
}

void SessionStorage::OnPageChange(const std::string& key,
                                  const std::string& value) {
  auto story_data = fuchsia::modular::internal::StoryData::New();
  if (!XdrRead(value, &story_data, XdrStoryData)) {
    FXL_LOG(ERROR) << "SessionStorage::OnPageChange : could not decode ledger "
                      "value for key "
                   << key << "\nvalue:\n"
                   << value;
    return;
  }

  auto story_id = StoryIdFromLedgerKey(key);
  if (on_story_updated_) {
    on_story_updated_(std::move(story_id), std::move(*story_data));
  }
}

void SessionStorage::OnPageDelete(const std::string& key) {
  if (on_story_deleted_) {
    // Call to StoryIdFromLedgerKey() needed because a deleted story is
    // modelled by deleting the key, and then the value is not available.
    // TODO(thatguy,mesch): Change PageClient to supply values of deleted keys
    // and/or change modeling of deleted stories.
    on_story_deleted_(StoryIdFromLedgerKey(key));
  }
}

}  // namespace modular
