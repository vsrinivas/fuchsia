// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/story_runner/session_storage.h"

#include <fuchsia/modular/internal/cpp/fidl.h>

#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/user_runner/story_runner/session_storage_xdr.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/ledger_client/operations.h"
#include "peridot/lib/ledger_client/storage.h"

namespace fuchsia {
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
    ledger::Page* const page, fidl::StringPtr story_id,
    std::function<void(modular::internal ::StoryDataPtr)> result_call) {
  return new ReadDataCall<modular::internal ::StoryData>(
      page, StoryIdToLedgerKey(story_id), true /* not_found_is_ok */,
      XdrStoryData, std::move(result_call));
};

OperationBase* MakeWriteStoryDataCall(
    ledger::Page* const page, modular::internal ::StoryDataPtr story_data,
    std::function<void()> result_call) {
  return new WriteDataCall<modular::internal ::StoryData>(
      page, StoryIdToLedgerKey(story_data->story_info.id), XdrStoryData,
      std::move(story_data), std::move(result_call));
};

class CreateStoryCall
    : public LedgerOperation<fidl::StringPtr, ledger::PageId> {
 public:
  CreateStoryCall(ledger::Ledger* const ledger, ledger::Page* const root_page,
                  fidl::VectorPtr<StoryInfoExtraEntry> extra_info,
                  ResultCall result_call)
      : LedgerOperation("SessionStorage::CreateStoryCall", ledger, root_page,
                        std::move(result_call)),
        extra_info_(std::move(extra_info)) {}

 private:
  void Run() override {
    FlowToken flow{this, &story_id_, &story_page_id_};

    ledger()->GetPage(nullptr, story_page_.NewRequest(),
                      Protect([this, flow](ledger::Status status) {
                        if (status != ledger::Status::OK) {
                          FXL_LOG(ERROR) << trace_name() << " "
                                         << "Ledger.GetPage() " << status;
                          return;
                        }

                        Cont1(flow);
                      }));
  }

  void Cont1(FlowToken flow) {
    story_page_->GetId([this, flow](ledger::PageId id) {
      story_page_id_ = std::move(id);
      Cont2(flow);
    });
  }

  void Cont2(FlowToken flow) {
    // TODO(security), cf. FW-174. This ID is exposed in public services
    // such as StoryProvider.PreviousStories(), StoryController.GetInfo(),
    // ModuleContext.GetStoryId(). We need to ensure this doesn't expose
    // internal information by being a page ID.
    // TODO(thatguy): Generate a GUID instead.
    story_id_ = to_hex_string(story_page_id_.id);

    story_data_ = modular::internal ::StoryData::New();
    story_data_->story_page_id = CloneOptional(story_page_id_);
    story_data_->story_info.id = story_id_;
    story_data_->story_info.last_focus_time = 0;
    story_data_->story_info.extra = std::move(extra_info_);

    operation_queue_.Add(MakeWriteStoryDataCall(page(), std::move(story_data_),
                                                [this, flow] {}));
  }

  fidl::VectorPtr<StoryInfoExtraEntry> extra_info_;

  ledger::PagePtr story_page_;
  modular::internal ::StoryDataPtr story_data_;

  ledger::PageId story_page_id_;
  fidl::StringPtr story_id_;  // This is the result of the Operation.

  // Sub operations run in this queue.
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CreateStoryCall);
};

}  // namespace

FuturePtr<fidl::StringPtr, ledger::PageId> SessionStorage::CreateStory(
    fidl::VectorPtr<StoryInfoExtraEntry> extra_info) {
  auto ret = Future<fidl::StringPtr, ledger::PageId>::Create();
  operation_queue_.Add(new CreateStoryCall(ledger_client_->ledger(), page(),
                                           std::move(extra_info), ret->Completer()));
  return ret;
}

FuturePtr<> SessionStorage::DeleteStory(fidl::StringPtr story_id) {
  auto on_run = Future<>::Create();
  auto done = on_run->AsyncMap([this, story_id] {
    auto deleted = Future<>::Create();
    page()->Delete(to_array(StoryIdToLedgerKey(story_id)),
                   [this, deleted](ledger::Status status) {
                     // Deleting a key that doesn't exist is OK, not
                     // KEY_NOT_FOUND.
                     if (status != ledger::Status::OK) {
                       FXL_LOG(ERROR)
                           << "SessionStorage: Page.Delete() " << status;
                     }
                     deleted->Complete();
                   });

    return deleted;
  });
  auto ret = Future<>::Create();
  operation_queue_.Add(WrapFutureAsOperation(on_run, done, ret->Completer(),
                                             "SessionStorage::DeleteStory"));
  return ret;
}

namespace {

class MutateStoryDataCall : public Operation<> {
 public:
  MutateStoryDataCall(
      ledger::Page* const page, fidl::StringPtr story_id,
      std::function<bool(modular::internal ::StoryData* story_data)> mutate,
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
        [this, flow](modular::internal ::StoryDataPtr story_data) {
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

  ledger::Page* const page_;  // not owned
  const fidl::StringPtr story_id_;
  std::function<bool(modular::internal ::StoryData* story_data)> mutate_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MutateStoryDataCall);
};

}  // namespace

FuturePtr<> SessionStorage::UpdateLastFocusedTimestamp(fidl::StringPtr story_id,
                                                       const int64_t ts) {
  auto mutate = [ts](modular::internal ::StoryData* const story_data) {
    if (story_data->story_info.last_focus_time == ts) {
      return false;
    }
    story_data->story_info.last_focus_time = ts;
    return true;
  };

  auto ret = Future<>::Create();
  operation_queue_.Add(
      new MutateStoryDataCall(page(), story_id, mutate, ret->Completer()));
  return ret;
}

FuturePtr<modular::internal ::StoryDataPtr> SessionStorage::GetStoryData(
    fidl::StringPtr story_id) {
  auto ret = Future<modular::internal ::StoryDataPtr>::Create();
  operation_queue_.Add(
      MakeGetStoryDataCall(page(), story_id, ret->Completer()));
  return ret;
}

// Returns a Future vector of StoryData for all stories in this session.
FuturePtr<fidl::VectorPtr<modular::internal ::StoryData>>
SessionStorage::GetAllStoryData() {
  auto ret = Future<fidl::VectorPtr<modular::internal ::StoryData>>::Create();
  operation_queue_.Add(new ReadAllDataCall<modular::internal ::StoryData>(
      page(), kStoryKeyPrefix, XdrStoryData, ret->Completer()));
  return ret;
}

void SessionStorage::OnPageChange(const std::string& key,
                                  const std::string& value) {
  auto story_data = modular::internal ::StoryData::New();
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
}  // namespace fuchsia
