// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/story_provider_impl.h"

#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/strong_binding.h"
#include "apps/modular/src/user_runner/story_controller_impl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/ftl/functional/make_copyable.h"

namespace modular {
namespace {

// Generates a unique randomly generated string of |length| size to be
// used as a story id.
std::string MakeStoryId(std::unordered_set<std::string>* story_ids,
                        const size_t length) {
  std::function<char()> randchar = []() -> char {
    const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    const size_t max_index = (sizeof(charset) - 1);
    return charset[rand() % max_index];
  };

  std::string id(length, 0);
  std::generate_n(id.begin(), length, randchar);

  if (story_ids->find(id) != story_ids->end()) {
    return MakeStoryId(story_ids, length);
  }

  story_ids->insert(id);
  return id;
}

// Below are helper classes that encapsulates a chain of asynchronous
// operations on the Ledger. Because the operations all return
// something, the handles on which they are invoked need to be kept
// around until the return value arrives. This precludes them to be
// local variables. The next thing that comes to mind is to make them
// fields of the containing object. However, there might be multiple
// such operations going on concurrently in one StoryProviderImpl, so
// they cannot be fields of StoryProviderImpl. Thus such operations
// are separate classes for now, until I can think of something
// better, or we change the API to interface requests.
//
// NOTE(mesch): After these classes were written, the API was changed
// to InterfaceRequests. Most of the nesting can be removed now,
// unless we want to check status, which is still returned. Status
// checking was useful in debugging ledger, so I let the nesting in
// place for now.

class GetStoryDataCall : public Transaction {
 public:
  using Result = std::function<void(StoryDataPtr)>;

  GetStoryDataCall(TransactionContainer* const container,
                   ledger::Ledger* const ledger,
                   const fidl::String& story_id,
                   Result result)
      : Transaction(container),
        ledger_(ledger),
        story_id_(story_id),
        result_(result) {
    ledger_->GetRootPage(GetProxy(&root_page_), [this](ledger::Status status) {
      if (status != ledger::Status::OK) {
        FTL_LOG(ERROR) << "GetStoryDataCall() " << story_id_
                       << " Ledger.GetRootPage() " << status;
        Done();
        return;
      }

      root_page_->GetSnapshot(
          GetProxy(&root_snapshot_), [this](ledger::Status status) {
            if (status != ledger::Status::OK) {
              FTL_LOG(ERROR) << "GetStoryDataCall() " << story_id_
                             << " Page.GetSnapshot() " << status;
              Done();
              return;
            }

            root_snapshot_->Get(
                to_array(story_id_),
                [this](ledger::Status status, ledger::ValuePtr value) {
                  if (status != ledger::Status::OK) {
                    FTL_LOG(ERROR) << "GetStoryDataCall() " << story_id_
                                   << " PageSnapshot.Get() " << status;
                    Done();
                    return;
                  }

                  story_data_ = StoryData::New();
                  story_data_->Deserialize(value->get_bytes().data(),
                                           value->get_bytes().size());

                  result_(std::move(story_data_));
                  Done();
                });
          });
    });
  };

 private:
  ledger::Ledger* const ledger_;  // not owned
  const fidl::String story_id_;
  Result result_;

  ledger::PagePtr root_page_;
  ledger::PageSnapshotPtr root_snapshot_;
  StoryDataPtr story_data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GetStoryDataCall);
};

class WriteStoryDataCall : public Transaction {
 public:
  using Result = std::function<void()>;

  WriteStoryDataCall(TransactionContainer* const container,
                     ledger::Ledger* const ledger,
                     StoryDataPtr story_data,
                     Result result)
      : Transaction(container),
        ledger_(ledger),
        story_data_(std::move(story_data)),
        result_(result) {
    ledger_->GetRootPage(GetProxy(&root_page_), [this](ledger::Status status) {

      const size_t size = story_data_->GetSerializedSize();
      fidl::Array<uint8_t> value = fidl::Array<uint8_t>::New(size);
      story_data_->Serialize(value.data(), size);

      const fidl::String& story_id = story_data_->story_info->id;
      root_page_->PutWithPriority(to_array(story_id), std::move(value),
                                  ledger::Priority::EAGER,
                                  [this](ledger::Status status) {
                                    result_();
                                    Done();
                                  });
    });
  }

 private:
  ledger::Ledger* const ledger_;  // not owned
  StoryDataPtr story_data_;
  ledger::PagePtr root_page_;
  Result result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(WriteStoryDataCall);
};

class CreateStoryCall : public Transaction {
 public:
  CreateStoryCall(
      TransactionContainer* const container,
      ledger::Ledger* const ledger,
      ApplicationEnvironment* const environment,
      StoryProviderImpl* const story_provider_impl,
      const fidl::String& url,
      const std::string& story_id,
      fidl::InterfaceRequest<StoryController> story_controller_request)
      : Transaction(container),
        ledger_(ledger),
        environment_(environment),
        story_provider_impl_(story_provider_impl),
        url_(url),
        story_id_(story_id),
        story_controller_request_(std::move(story_controller_request)) {
    ledger_->NewPage(GetProxy(&story_page_), [this](ledger::Status status) {
      story_page_->GetId([this](fidl::Array<uint8_t> story_page_id) {
        story_data_ = StoryData::New();
        story_data_->story_page_id = std::move(story_page_id);
        story_data_->story_info = StoryInfo::New();
        auto* const story_info = story_data_->story_info.get();
        story_info->url = url_;
        story_info->id = story_id_;
        story_info->is_running = false;
        story_info->state = StoryState::NEW;
        story_info->extra.mark_non_null();

        story_provider_impl_->WriteStoryData(story_data_->Clone(), [this]() {
          ApplicationLauncherPtr launcher;
          environment_->GetApplicationLauncher(fidl::GetProxy(&launcher));
          StoryControllerImpl::New(std::move(story_data_), story_provider_impl_,
                                   std::move(launcher),
                                   std::move(story_controller_request_));
          Done();
        });
      });
    });
  }

 private:
  ledger::Ledger* const ledger_;  // not owned
  ApplicationEnvironment* const environment_;
  StoryProviderImpl* const story_provider_impl_;  // not owned
  const fidl::String url_;
  const std::string story_id_;
  fidl::InterfaceRequest<StoryController> story_controller_request_;

  ledger::PagePtr story_page_;
  StoryDataPtr story_data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CreateStoryCall);
};

// Deletes a story given its id.
class DeleteStoryCall : public Transaction {
 public:
  using Result = StoryProviderImpl::DeleteStoryCallback;

  DeleteStoryCall(TransactionContainer* const container,
                  ledger::Ledger* const ledger,
                  const fidl::String& story_id,
                  Result result)
      : Transaction(container),
        ledger_(ledger),
        story_id_(story_id),
        result_(result) {
    ledger_->GetRootPage(GetProxy(&root_page_), [this](ledger::Status status) {
      root_page_->Delete(to_array(story_id_),
                         [this](ledger::Status ledger_status) {
                           result_();
                           Done();
                         });
    });
  }

 private:
  ledger::Ledger* const ledger_;  // not owned
  ledger::PagePtr root_page_;
  const fidl::String story_id_;
  Result result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DeleteStoryCall);
};

class ResumeStoryCall : public Transaction {
 public:
  ResumeStoryCall(
      TransactionContainer* const container,
      ledger::Ledger* const ledger,
      ApplicationEnvironment* const environment,
      StoryProviderImpl* const story_provider_impl,
      const fidl::String& story_id,
      fidl::InterfaceRequest<StoryController> story_controller_request)
      : Transaction(container),
        ledger_(ledger),
        environment_(environment),
        story_provider_impl_(story_provider_impl),
        story_id_(story_id),
        story_controller_request_(std::move(story_controller_request)) {
    story_provider_impl_->GetStoryData(
        story_id_, [this](StoryDataPtr story_data) {
          story_data_ = std::move(story_data);
          ledger_->GetPage(
              story_data_->story_page_id.Clone(), GetProxy(&story_page_),
              [this](ledger::Status status) {
                ApplicationLauncherPtr launcher;
                environment_->GetApplicationLauncher(fidl::GetProxy(&launcher));
                StoryControllerImpl::New(
                    std::move(story_data_), story_provider_impl_,
                    std::move(launcher), std::move(story_controller_request_));

                Done();
              });
        });
  }

 private:
  ledger::Ledger* const ledger_;  // not owned
  ApplicationEnvironment* const environment_;
  StoryProviderImpl* const story_provider_impl_;  // not owned
  const fidl::String story_id_;
  fidl::InterfaceRequest<StoryController> story_controller_request_;

  StoryDataPtr story_data_;
  ledger::PagePtr story_page_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ResumeStoryCall);
};

class PreviousStoriesCall : public Transaction {
 public:
  using Result = StoryProviderImpl::PreviousStoriesCallback;

  PreviousStoriesCall(TransactionContainer* const container,
                      ledger::Ledger* const ledger,
                      Result result)
      : Transaction(container), ledger_(ledger), result_(result) {
    ledger_->GetRootPage(GetProxy(&root_page_), [this](ledger::Status status) {
      root_page_->GetSnapshot(
          GetProxy(&root_snapshot_), [this](ledger::Status status) {
            root_snapshot_->GetEntries(
                nullptr, nullptr, [this](ledger::Status status,
                                         fidl::Array<ledger::EntryPtr> entries,
                                         fidl::Array<uint8_t> next_token) {
                  // TODO(mesch): Account for possibly
                  // continuation here. That's not just a matter
                  // of repeatedly calling, but it needs to be
                  // wired up to the API, because a list that is
                  // too large to return from Ledger is also too
                  // large to return from StoryProvider.
                  fidl::Array<fidl::String> story_ids;
                  // This resize() has the side effect of marking the array as
                  // non-null. Do not remove it because the fidl declaration
                  // of this return value does not allow nulls.
                  story_ids.resize(0);
                  for (auto& entry : entries) {
                    StoryDataPtr story_data = StoryData::New();
                    story_data->Deserialize(entry->value.data(),
                                            entry->value.size());
                    story_ids.push_back(story_data->story_info->id);
                  }

                  result_(std::move(story_ids));
                  Done();
                });
          });
    });
  }

 private:
  ledger::Ledger* const ledger_;  // not owned
  Result result_;
  ledger::PagePtr root_page_;
  ledger::PageSnapshotPtr root_snapshot_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PreviousStoriesCall);
};

}  // namespace

// TODO(alhaad): The current implementation makes no use of |PageWatcher| and
// assumes that only one device can access a user's ledger. Re-visit this
// assumption.
StoryProviderImpl::StoryProviderImpl(
    ApplicationEnvironmentPtr environment,
    fidl::InterfaceHandle<ledger::Ledger> ledger,
    fidl::InterfaceRequest<StoryProvider> story_provider_request)
    : environment_(std::move(environment)),
      binding_(this, std::move(story_provider_request)),
      storage_(new Storage) {
  ledger_.Bind(std::move(ledger));
}

// |StoryProvider|
void StoryProviderImpl::GetStoryInfo(
    const fidl::String& story_id,
    const GetStoryInfoCallback& story_data_callback) {
  new GetStoryDataCall(&transaction_container_, ledger_.get(), story_id,
                       [this, story_data_callback](StoryDataPtr story_data) {
                         story_data_callback(std::move(story_data->story_info));
                       });
}

void StoryProviderImpl::GetStoryData(
    const fidl::String& story_id,
    const std::function<void(StoryDataPtr)>& result) {
  new GetStoryDataCall(&transaction_container_, ledger_.get(), story_id, result);
}

ledger::PagePtr StoryProviderImpl::GetStoryPage(
    const fidl::Array<uint8_t>& story_page_id) {
  ledger::PagePtr ret;
  ledger_->GetPage(story_page_id.Clone(), GetProxy(&ret),
                   [](ledger::Status status) {
                     FTL_LOG(INFO) << "GetStoryPage() status " << status;
                   });

  return ret;
}

void StoryProviderImpl::WriteStoryData(StoryDataPtr story_data,
                                       std::function<void()> done) {
  new WriteStoryDataCall(&transaction_container_, ledger_.get(),
                         std::move(story_data), done);
}

// |StoryProvider|
void StoryProviderImpl::CreateStory(
    const fidl::String& url,
    fidl::InterfaceRequest<StoryController> story_controller_request) {
  const std::string story_id = MakeStoryId(&story_ids_, 10);
  new CreateStoryCall(&transaction_container_, ledger_.get(),
                      environment_.get(), this, url, story_id,
                      std::move(story_controller_request));
}

// |StoryProvider|
void StoryProviderImpl::DeleteStory(const fidl::String& story_id,
                                    const DeleteStoryCallback& callback) {
  new DeleteStoryCall(&transaction_container_, ledger_.get(), story_id,
                      callback);
}

// |StoryProvider|
void StoryProviderImpl::ResumeStory(
    const fidl::String& story_id,
    fidl::InterfaceRequest<StoryController> story_controller_request) {
  new ResumeStoryCall(&transaction_container_, ledger_.get(),
                      environment_.get(), this, story_id,
                      std::move(story_controller_request));
}

// |StoryProvider|
void StoryProviderImpl::PreviousStories(
    const PreviousStoriesCallback& callback) {
  new PreviousStoriesCall(&transaction_container_, ledger_.get(), callback);
}

}  // namespace modular
