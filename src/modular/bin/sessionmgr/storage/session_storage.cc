// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/session_storage.h"

#include <lib/fidl/cpp/clone.h>
#include <zircon/status.h>

#include <unordered_set>

#include "fuchsia/ledger/cpp/fidl.h"
#include "peridot/lib/ledger_client/operations.h"
#include "src/lib/uuid/uuid.h"
#include "src/modular/bin/sessionmgr/annotations.h"
#include "src/modular/bin/sessionmgr/storage/constants_and_utils.h"
#include "src/modular/bin/sessionmgr/storage/session_storage_xdr.h"
#include "src/modular/lib/fidl/clone.h"

namespace modular {

SessionStorage::SessionStorage(LedgerClient* ledger_client, LedgerPageId page_id)
    : PageClient("SessionStorage", ledger_client, page_id, kStoryKeyPrefix),
      ledger_client_(ledger_client) {
  FXL_DCHECK(ledger_client_ != nullptr);
}

namespace {

// TODO(rosswang): replace with |std::string::starts_with| after C++20
bool StartsWith(const std::string& string, const std::string& prefix) {
  return string.compare(0, prefix.size(), prefix) == 0;
}

std::string StoryNameToStoryDataKey(fidl::StringPtr story_name) {
  // Not escaped, because only one component after the prefix.
  return kStoryDataKeyPrefix + story_name.value_or("");
}

std::string StoryNameFromStoryDataKey(fidl::StringPtr key) {
  return key->substr(sizeof(kStoryDataKeyPrefix) - 1);
}

fuchsia::ledger::PageId ToPageId(const std::string& value) {
  fuchsia::ledger::PageId page_id;
  FXL_DCHECK(value.size() == page_id.id.size())
      << "value is of size: " << value.size() << ", expected size: " << page_id.id.size();
  memcpy(&page_id.id[0], value.data(), std::min(page_id.id.size(), value.size()));
  return page_id;
}

std::unique_ptr<OperationBase> MakeGetStoryDataCall(
    fuchsia::ledger::Page* const page, fidl::StringPtr story_name,
    fit::function<void(fuchsia::modular::internal::StoryDataPtr)> result_call) {
  return std::make_unique<ReadDataCall<fuchsia::modular::internal::StoryData>>(
      page, StoryNameToStoryDataKey(story_name), true /* not_found_is_ok */, XdrStoryData,
      std::move(result_call));
};

std::unique_ptr<OperationBase> MakeWriteStoryDataCall(
    fuchsia::ledger::Page* const page, fuchsia::modular::internal::StoryDataPtr story_data,
    fit::function<void()> result_call) {
  return std::make_unique<WriteDataCall<fuchsia::modular::internal::StoryData>>(
      page, StoryNameToStoryDataKey(story_data->story_info().id()), XdrStoryData,
      std::move(story_data), std::move(result_call));
};

class CreateStoryCall : public LedgerOperation<fidl::StringPtr, fuchsia::ledger::PageId> {
 public:
  CreateStoryCall(fuchsia::ledger::Ledger* const ledger, fuchsia::ledger::Page* const session_page,
                  fidl::StringPtr story_name, std::vector<fuchsia::modular::Annotation> annotations,
                  ResultCall result_call)
      : LedgerOperation("SessionStorage::CreateStoryCall", ledger, session_page,
                        std::move(result_call)),
        session_page_(std::move(session_page)),
        story_name_(std::move(story_name)),
        annotations_(std::move(annotations)) {}

 private:
  void Run() override {
    FlowToken flow{this, &story_name_, &story_page_id_};

    // Try to get any existing StoryData for the story with the given [story_name_] first.
    // If the StoryData exists, the story has already been created and should not be recreated.
    operation_queue_.Add(
        MakeGetStoryDataCall(session_page_, story_name_, [this, flow](auto story_data) {
          if (story_data) {
            // A story with the same name already exists, don't create it again.
            story_page_id_ = ToPageId(story_data->story_page_id());
            return;
          }

          ledger()->GetPage(nullptr, story_page_.NewRequest());
          story_page_->GetId([this, flow](fuchsia::ledger::PageId id) {
            story_page_id_ = std::move(id);
            Cont(flow);
          });
        }));
  }

  void Cont(FlowToken flow) {
    // TODO(security), cf. FW-174. This ID is exposed in public services
    // fuchsia::modular::StoryController.GetInfo(),
    // fuchsia::modular::ModuleContext.GetStoryName(). We need to ensure this
    // doesn't expose internal information by being a page ID.
    // TODO(thatguy): Generate a GUID instead.
    if (!story_name_ || story_name_->empty()) {
      story_name_ = uuid::Generate();
    }

    story_data_ = fuchsia::modular::internal::StoryData::New();
    story_data_->set_story_name(story_name_.value_or(""));
    story_data_->set_story_page_id(to_string(story_page_id_.id));
    story_data_->mutable_story_info()->set_id(story_name_.value_or(""));
    story_data_->mutable_story_info()->set_last_focus_time(0);
    if (!annotations_.empty()) {
      story_data_->mutable_story_info()->set_annotations(std::move(annotations_));
    }
    operation_queue_.Add(MakeWriteStoryDataCall(page(), std::move(story_data_), [flow] {}));
  }

  fuchsia::ledger::Page* const session_page_;  // not owned
  fidl::StringPtr story_name_;
  std::vector<fuchsia::modular::Annotation> annotations_;

  fuchsia::ledger::PagePtr story_page_;
  fuchsia::modular::internal::StoryDataPtr story_data_;

  fuchsia::ledger::PageId story_page_id_;

  // Sub operations run in this queue.
  OperationQueue operation_queue_;
};

}  // namespace

FuturePtr<fidl::StringPtr, fuchsia::ledger::PageId> SessionStorage::CreateStory(
    fidl::StringPtr story_name, std::vector<fuchsia::modular::Annotation> annotations) {
  auto ret =
      Future<fidl::StringPtr, fuchsia::ledger::PageId>::Create("SessionStorage.CreateStory.ret");
  operation_queue_.Add(std::make_unique<CreateStoryCall>(ledger_client_->ledger(), page(),
                                                         std::move(story_name),
                                                         std::move(annotations), ret->Completer()));
  return ret;
}

FuturePtr<fidl::StringPtr, fuchsia::ledger::PageId> SessionStorage::CreateStory(
    std::vector<fuchsia::modular::Annotation> annotations) {
  return CreateStory(/*story_name=*/nullptr, std::move(annotations));
}

namespace {
class DeleteStoryCall : public Operation<> {
 public:
  DeleteStoryCall(fuchsia::ledger::Ledger* const ledger, fuchsia::ledger::Page* const session_page,
                  fidl::StringPtr story_name, ResultCall result_call)
      : Operation("SessionStorage::DeleteStoryCall", std::move(result_call)),
        ledger_(ledger),
        session_page_(session_page),
        story_name_(story_name) {}

 private:
  void Run() override {
    FlowToken flow{this};
    operation_queue_.Add(
        MakeGetStoryDataCall(session_page_, story_name_, [this, flow](auto story_data) {
          if (!story_data)
            return;
          story_data_ = std::move(*story_data);
          Cont(flow);
        }));
  }

  void Cont(FlowToken flow) {
    // Get the story page so we can remove its contents.
    ledger_->GetPage(
        std::make_unique<fuchsia::ledger::PageId>(ToPageId(story_data_.story_page_id())),
        story_page_.NewRequest());
    story_page_->Clear();
    // Remove the story data in the session page.
    session_page_->Delete(to_array(StoryNameToStoryDataKey(story_data_.story_info().id())));
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
  operation_queue_.Add(std::make_unique<DeleteStoryCall>(ledger_client_->ledger(), page(),
                                                         story_name, ret->Completer()));
  return ret;
}

namespace {
class MutateStoryDataCall : public Operation<> {
 public:
  MutateStoryDataCall(fuchsia::ledger::Page* const page, fidl::StringPtr story_name,
                      fit::function<bool(fuchsia::modular::internal::StoryData* story_data)> mutate,
                      ResultCall result_call)
      : Operation("SessionStorage::MutateStoryDataCall", std::move(result_call)),
        page_(page),
        story_name_(story_name),
        mutate_(std::move(mutate)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    operation_queue_.Add(MakeGetStoryDataCall(
        page_, story_name_, [this, flow](fuchsia::modular::internal::StoryDataPtr story_data) {
          if (!story_data) {
            // If the story doesn't exist, it was deleted.
            return;
          }
          if (!mutate_(story_data.get())) {
            // If no mutation happened, we're done.
            return;
          }

          operation_queue_.Add(MakeWriteStoryDataCall(page_, std::move(story_data), [flow] {}));
        }));
  }

  fuchsia::ledger::Page* const page_;  // not owned
  const fidl::StringPtr story_name_;
  fit::function<bool(fuchsia::modular::internal::StoryData* story_data)> mutate_;

  OperationQueue operation_queue_;
};

}  // namespace

FuturePtr<> SessionStorage::UpdateLastFocusedTimestamp(fidl::StringPtr story_name,
                                                       const int64_t ts) {
  auto mutate = [ts](fuchsia::modular::internal::StoryData* const story_data) {
    if (story_data->story_info().last_focus_time() == ts) {
      return false;
    }
    story_data->mutable_story_info()->set_last_focus_time(ts);
    return true;
  };

  auto ret = Future<>::Create("SessionStorage.UpdateLastFocusedTimestamp.ret");
  operation_queue_.Add(
      std::make_unique<MutateStoryDataCall>(page(), story_name, mutate, ret->Completer()));
  return ret;
}

FuturePtr<fuchsia::modular::internal::StoryDataPtr> SessionStorage::GetStoryData(
    fidl::StringPtr story_name) {
  auto ret =
      Future<fuchsia::modular::internal::StoryDataPtr>::Create("SessionStorage.GetStoryData.ret");
  operation_queue_.Add(MakeGetStoryDataCall(page(), story_name, ret->Completer()));
  return ret;
}

// Returns a Future vector of StoryData for all stories in this session.
FuturePtr<std::vector<fuchsia::modular::internal::StoryData>> SessionStorage::GetAllStoryData() {
  auto ret = Future<std::vector<fuchsia::modular::internal::StoryData>>::Create(
      "SessionStorage.GetAllStoryData.ret");
  operation_queue_.Add(std::make_unique<ReadAllDataCall<fuchsia::modular::internal::StoryData>>(
      page(), kStoryDataKeyPrefix, XdrStoryData, ret->Completer()));
  return ret;
}

FuturePtr<> SessionStorage::UpdateStoryAnnotations(
    fidl::StringPtr story_name, std::vector<fuchsia::modular::Annotation> annotations) {
  auto ret = Future<>::Create("SessionStorage.UpdateStoryAnnotations.ret");
  auto mutate = [annotations = std::move(annotations)](
                    fuchsia::modular::internal::StoryData* story_data) mutable {
    story_data->mutable_story_info()->set_annotations(std::move(annotations));
    return true;
  };
  operation_queue_.Add(std::make_unique<MutateStoryDataCall>(page(), story_name, std::move(mutate),
                                                             ret->Completer()));
  return ret;
}

FuturePtr<std::optional<fuchsia::modular::AnnotationError>> SessionStorage::MergeStoryAnnotations(
    fidl::StringPtr story_name, std::vector<fuchsia::modular::Annotation> annotations) {
  auto ret = Future<std::optional<fuchsia::modular::AnnotationError>>::Create(
      "SessionStorage.MergeStoryAnnotations.ret");
  // On success, this optional AnnotationError response will have no value (!has_value()).
  // Otherwise, the error will be set explicitly, or it is assumed the story is no longer viable
  // (default NOT_FOUND).
  auto error_ptr = std::make_unique<std::optional<fuchsia::modular::AnnotationError>>(
      fuchsia::modular::AnnotationError::NOT_FOUND);
  auto mutate = [error_ptr = error_ptr.get(), annotations = std::move(annotations)](
                    fuchsia::modular::internal::StoryData* story_data) mutable {
    auto new_annotations =
        story_data->story_info().has_annotations()
            ? annotations::Merge(
                  std::move(*story_data->mutable_story_info()->mutable_annotations()),
                  std::move(annotations))
            : std::move(annotations);
    if (new_annotations.size() > fuchsia::modular::MAX_ANNOTATIONS_PER_STORY) {
      *error_ptr = fuchsia::modular::AnnotationError::TOO_MANY_ANNOTATIONS;
      return false;
    }
    story_data->mutable_story_info()->set_annotations(std::move(new_annotations));
    error_ptr->reset();
    return true;
  };
  operation_queue_.Add(std::make_unique<MutateStoryDataCall>(
      page(), story_name, std::move(mutate),
      [ret, error_ptr = std::move(error_ptr)]() mutable { ret->Complete(std::move(*error_ptr)); }));
  return ret;
}

FuturePtr<std::unique_ptr<StoryStorage>> SessionStorage::GetStoryStorage(
    fidl::StringPtr story_name) {
  auto returned_future = Future<std::unique_ptr<StoryStorage>>::Create(
      "SessionStorage.GetStoryStorage.returned_future");

  operation_queue_.Add(MakeGetStoryDataCall(
      page(), story_name,
      [this, returned_future, story_name](fuchsia::modular::internal::StoryDataPtr story_data) {
        if (story_data) {
          auto story_storage =
              std::make_unique<StoryStorage>(ledger_client_, ToPageId(story_data->story_page_id()));
          returned_future->Complete(std::move(story_storage));
        } else {
          returned_future->Complete(nullptr);
        }
      }));

  return returned_future;
}

void SessionStorage::OnPageChange(const std::string& key, const std::string& value) {
  if (StartsWith(key, kStoryDataKeyPrefix)) {
    auto story_data = fuchsia::modular::internal::StoryData::New();
    if (!XdrRead(value, &story_data, XdrStoryData)) {
      FXL_LOG(ERROR) << "SessionStorage::OnPageChange : could not decode ledger "
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
