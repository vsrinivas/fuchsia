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
// local variabes. The next thing that comes to mind is to make them
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

class GetStoryInfoCall : public Transaction {
 public:
  using Result = std::function<void(StoryInfoPtr)>;

  GetStoryInfoCall(TransactionContainer* const container,
                   ledger::Ledger* const ledger,
                   const fidl::String& story_id,
                   Result result)
      : Transaction(container),
        ledger_(ledger),
        story_id_(story_id),
        result_(result) {
    ledger_->GetRootPage(GetProxy(&root_page_), [this](ledger::Status status) {
      if (status != ledger::Status::OK) {
        FTL_LOG(ERROR) << "GetStoryInfoCall() " << story_id_
                       << " Ledger.GetRootPage() " << status;
        Done();
        return;
      }

      root_page_->GetSnapshot(
          GetProxy(&root_snapshot_), [this](ledger::Status status) {
            if (status != ledger::Status::OK) {
              FTL_LOG(ERROR) << "GetStoryInfoCall() " << story_id_
                             << " Page.GetSnapshot() " << status;
              Done();
              return;
            }

            root_snapshot_->Get(
                to_array(story_id_),
                [this](ledger::Status status, ledger::ValuePtr value) {
                  if (status != ledger::Status::OK) {
                    FTL_LOG(ERROR) << "GetStoryInfoCall() " << story_id_
                                   << " PageSnapshot.Get() " << status;
                    Done();
                    return;
                  }

                  story_info_ = StoryInfo::New();
                  story_info_->Deserialize(value->get_bytes().data(),
                                           value->get_bytes().size());

                  result_(std::move(story_info_));
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
  StoryInfoPtr story_info_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GetStoryInfoCall);
};

class WriteStoryInfoCall : public Transaction {
 public:
  using Result = std::function<void()>;

  WriteStoryInfoCall(TransactionContainer* const container,
                     ledger::Ledger* const ledger,
                     StoryInfoPtr story_info,
                     Result result)
      : Transaction(container),
        ledger_(ledger),
        story_info_(std::move(story_info)),
        result_(result) {
    ledger_->GetRootPage(GetProxy(&root_page_), [this](ledger::Status status) {

      const size_t size = story_info_->GetSerializedSize();
      fidl::Array<uint8_t> value = fidl::Array<uint8_t>::New(size);
      story_info_->Serialize(value.data(), size);

      const fidl::String& story_id = story_info_->id;
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
  StoryInfoPtr story_info_;
  ledger::PagePtr root_page_;
  Result result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(WriteStoryInfoCall);
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
        story_info_ = StoryInfo::New();
        story_info_->url = url_;
        story_info_->id = story_id_;
        story_info_->story_page_id = std::move(story_page_id);
        story_info_->is_running = false;

        story_provider_impl_->WriteStoryInfo(story_info_->Clone(), [this]() {
          ApplicationLauncherPtr launcher;
          environment_->GetApplicationLauncher(fidl::GetProxy(&launcher));
          StoryControllerImpl::New(std::move(story_info_), story_provider_impl_,
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
  StoryInfoPtr story_info_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CreateStoryCall);
};

class ResumeStoryCall : public Transaction {
 public:
  // Resumes a story given only its ID.
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
    story_provider_impl_->GetStoryInfo(
        story_id_, [this](StoryInfoPtr story_info) {
          story_info_ = std::move(story_info);
          ledger_->GetPage(
              story_info_->story_page_id.Clone(), GetProxy(&story_page_),
              [this](ledger::Status status) {
                ApplicationLauncherPtr launcher;
                environment_->GetApplicationLauncher(fidl::GetProxy(&launcher));
                StoryControllerImpl::New(
                    std::move(story_info_), story_provider_impl_,
                    std::move(launcher), std::move(story_controller_request_));

                Done();
              });
        });
  }

  // Resumes a story given its full story info. Compared to the
  // variant above, this saves to obtain the story info first.
  ResumeStoryCall(
      TransactionContainer* const container,
      ledger::Ledger* const ledger,
      ApplicationEnvironment* const environment,
      StoryProviderImpl* const story_provider_impl,
      StoryInfoPtr story_info,
      fidl::InterfaceRequest<StoryController> story_controller_request)
      : Transaction(container),
        ledger_(ledger),
        environment_(environment),
        story_provider_impl_(story_provider_impl),
        story_controller_request_(std::move(story_controller_request)),
        story_info_(std::move(story_info)) {
    ledger_->GetPage(
        story_info_->story_page_id.Clone(), GetProxy(&story_page_),
        [this](ledger::Status status) {
          ApplicationLauncherPtr launcher;
          environment_->GetApplicationLauncher(fidl::GetProxy(&launcher));
          StoryControllerImpl::New(std::move(story_info_), story_provider_impl_,
                                   std::move(launcher),
                                   std::move(story_controller_request_));

          Done();
        });
  }

 private:
  ledger::Ledger* const ledger_;  // not owned
  ApplicationEnvironment* const environment_;
  StoryProviderImpl* const story_provider_impl_;  // not owned
  const fidl::String story_id_;
  fidl::InterfaceRequest<StoryController> story_controller_request_;

  StoryInfoPtr story_info_;
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
                  for (auto& entry : entries) {
                    StoryInfoPtr story_info = StoryInfo::New();
                    story_info->Deserialize(entry->value.data(),
                                            entry->value.size());
                    story_ids.push_back(story_info->id);
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

void StoryProviderImpl::GetStoryInfo(
    const fidl::String& story_id,
    std::function<void(StoryInfoPtr story_info)> story_info_callback) {
  new GetStoryInfoCall(&transaction_container_, ledger_.get(), story_id,
                       story_info_callback);
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

void StoryProviderImpl::WriteStoryInfo(StoryInfoPtr story_info) {
  FTL_LOG(INFO) << "StoryProviderImpl::WriteStoryInfo() " << story_info->id;

  new WriteStoryInfoCall(&transaction_container_, ledger_.get(),
                         std::move(story_info), []() {});
}

void StoryProviderImpl::WriteStoryInfo(StoryInfoPtr story_info,
                                       std::function<void()> done) {
  FTL_LOG(INFO) << "StoryProviderImpl::WriteStoryInfo() " << story_info->id;

  new WriteStoryInfoCall(&transaction_container_, ledger_.get(),
                         std::move(story_info), done);
}

// |StoryProvider|
void StoryProviderImpl::CreateStory(
    const fidl::String& url,
    fidl::InterfaceRequest<StoryController> story_controller_request) {
  const std::string story_id = MakeStoryId(&story_ids_, 10);
  FTL_LOG(INFO) << "StoryProviderImpl::CreateStory() " << url << " "
                << story_id;
  new CreateStoryCall(&transaction_container_, ledger_.get(),
                      environment_.get(), this, url, story_id,
                      std::move(story_controller_request));
}

// |StoryProvider|
void StoryProviderImpl::ResumeStoryById(
    const fidl::String& story_id,
    fidl::InterfaceRequest<StoryController> story_controller_request) {
  FTL_LOG(INFO) << "StoryProviderImpl::ResumeStoryById() " << story_id;
  new ResumeStoryCall(&transaction_container_, ledger_.get(),
                      environment_.get(), this, story_id,
                      std::move(story_controller_request));
}

// |StoryProvider|
void StoryProviderImpl::ResumeStoryByInfo(
    StoryInfoPtr story_info,
    fidl::InterfaceRequest<StoryController> story_controller_request) {
  FTL_LOG(INFO) << "StoryProviderImpl::ResumeStoryByInfo() " << story_info->id;
  new ResumeStoryCall(&transaction_container_, ledger_.get(),
                      environment_.get(), this, std::move(story_info),
                      std::move(story_controller_request));
}

// |StoryProvider|
void StoryProviderImpl::PreviousStories(
    const PreviousStoriesCallback& callback) {
  FTL_LOG(INFO) << "StoryProviderImpl::PreviousStories()";
  new PreviousStoriesCall(&transaction_container_, ledger_.get(), callback);
}

}  // namespace modular
