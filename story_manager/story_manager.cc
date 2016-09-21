// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the story manager mojo app and of all mojo
// services it provides directly or transitively from other services.
// The mojom definitions for the services are in
// ../mojom_hack/story_manager.mojom, though they should be here.

#include <mojo/system/main.h>
#include <unordered_map>
#include <unordered_set>

#include "apps/modular/story_manager/story_manager.mojom.h"
#include "apps/modular/story_runner/story_runner.mojom.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace story_manager {

// A utility method to convert a string key into a byte mojo array.
mojo::Array<uint8_t> KeyToByteArray(const std::string& key) {
  mojo::Array<uint8_t> array = mojo::Array<uint8_t>::New(key.length());
  for (size_t i = 0; i < key.length(); i++) {
    array[i] = static_cast<uint8_t>(key[i]);
  }
  return array;
}

// |StoryState| and |StoryProviderState| are forward declarations of methods
// that don't have mojom interface methods for.
class StoryState {
 public:
  virtual mojo::StructPtr<StoryInfo> GetStoryInfo() const = 0;
  virtual void RunStory(mojo::InterfacePtr<ledger::Page> session_page) = 0;
};

class StoryProviderState {
 public:
  virtual void ResumeStoryState(StoryState* story_state) = 0;
  virtual void CommitStoryState(StoryState* story_state) = 0;
  virtual void RemoveStoryState(StoryState* story_state) = 0;
};

class StoryImpl : public Story, public StoryState {
 public:
  StoryImpl(mojo::StructPtr<StoryInfo> story_info,
            StoryProviderState* story_provider_state, mojo::Shell* shell,
            mojo::InterfaceRequest<Story> request)
      : story_info_(std::move(story_info)),
        story_provider_state_(story_provider_state),
        shell_(shell),
        binding_(this, std::move(request)) {}

  ~StoryImpl() override {
    story_provider_state_->CommitStoryState(this);
    story_provider_state_->RemoveStoryState(this);
  }

  // |StoryState| override.
  mojo::StructPtr<StoryInfo> GetStoryInfo() const override {
    return story_info_->Clone();
  }

  // Runs this story. If |session_page| is empty, we are effectively starting
  // a new session, else we are re-inflating an existing session.
  // This is responsible for commiting data to |session_page|.
  // TODO(alhaad): Define the interface for passing |session_page| to
  // story-runner.
  // |StoryState| override.
  void RunStory(mojo::InterfacePtr<ledger::Page> session_page) override {
    mojo::InterfacePtr<story::ResolverFactory> resolver_factory;
    mojo::ConnectToService(shell_, "mojo:component_manager",
                           mojo::GetProxy(&resolver_factory));
    mojo::ConnectToService(shell_, "mojo:story_runner",
                           mojo::GetProxy(&runner_));
    runner_->Initialize(resolver_factory.Pass());
    runner_->StartStory(GetProxy(&session_));
    mojo::InterfaceHandle<story::Link> link;
    session_->CreateLink("boot", GetProxy(&link));
    session_->StartModule(story_info_->url, std::move(link),
                          [this](mojo::InterfaceHandle<story::Module> m) {
                            module_.Bind(std::move(m));
                          });
    story_info_->is_running = true;
  }

 private:
  // |Story| override.
  void GetInfo(const GetInfoCallback& callback) override {
    callback.Run(story_info_->Clone());
  }

  // |Story| override.
  void Stop() override {
    if (!story_info_->is_running) {
      return;
    }

    module_.reset();
    session_.reset();
    runner_.reset();
    story_provider_state_->CommitStoryState(this);
    story_info_->is_running = false;
  }

  // |Story| override.
  void Resume() override {
    if (story_info_->is_running) {
      return;
    }

    story_provider_state_->ResumeStoryState(this);
  }

  mojo::StructPtr<StoryInfo> story_info_;
  StoryProviderState* story_provider_state_;
  mojo::Shell* shell_;
  mojo::StrongBinding<Story> binding_;

  mojo::InterfacePtr<story::Runner> runner_;
  mojo::InterfacePtr<story::Session> session_;
  mojo::InterfacePtr<story::Module> module_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryImpl);
};

// TODO(alhaad): The current implementation makes no use of |PageWatcher| and
// assumes that only one device can access a user's ledger. Re-visit this
// assumption.
class StoryProviderImpl : public StoryProvider, public StoryProviderState {
 public:
  StoryProviderImpl(mojo::Shell* shell,
                    mojo::InterfacePtr<ledger::Ledger> ledger,
                    mojo::InterfaceHandle<StoryProvider>* service)
      : shell_(shell), binding_(this, service), ledger_(std::move(ledger)) {
    ledger_->GetRootPage([this](ledger::Status status,
                                mojo::InterfaceHandle<ledger::Page> root_page) {
      if (status != ledger::Status::OK) {
        FTL_NOTREACHED() << "Ledger did not return root page. Unhandled error";
        return;
      }
      root_page_.Bind(root_page.Pass());
    });
  }

  ~StoryProviderImpl() override {}

 private:
  // Used to resume a story. Fetches the Session Page associated with
  // |story_state| and calls |RunStory|. This does not take ownership of
  // |story_state|.
  // |StoryProviderState| override.
  void ResumeStoryState(StoryState* story_state) override {
    auto info = story_state->GetStoryInfo();
    ledger_->GetPage(
        std::move(info->session_page_id),
        [story_state](ledger::Status status,
                      mojo::InterfaceHandle<ledger::Page> session_page) {
          story_state->RunStory(mojo::InterfacePtr<ledger::Page>::Create(
              std::move(session_page)));
        });
  }

  // Commits story meta-data to the ledger. This is used after calling
  // |Stop| or when the |Story| pipe is close. This does not take ownership
  // of |story_state|.
  // |StoryProviderState| override.
  void CommitStoryState(StoryState* story_state) override {
    auto info = story_state->GetStoryInfo();
    auto size = info->GetSerializedSize();
    mojo::Array<uint8_t> value = mojo::Array<uint8_t>::New(size);
    info->Serialize(value.data(), size);
    auto story_id = story_state_to_id_[story_state];
    root_page_->PutWithPriority(KeyToByteArray(story_id), std::move(value),
                                ledger::Priority::EAGER,
                                [](ledger::Status status) {});
  }

  // Removes all the in-memory data structures from |StoryProviderState|
  // associated with |story_state|. This does not take ownership of
  // |story_state|.
  // |StoryProviderState| override.
  virtual void RemoveStoryState(StoryState* story_state) override {
    auto story_id = story_state_to_id_[story_state];
    story_state_to_id_.erase(story_state);
    story_id_to_state_.erase(story_id);
    story_ids_.erase(story_id);
  }

  // |StoryProvider| override.
  void StartNewStory(const mojo::String& url,
                     const StartNewStoryCallback& callback) override {
    // TODO(alhaad): Creating multiple stories can only work after
    // https://fuchsia-review.googlesource.com/#/c/8941/ has landed.
    FTL_LOG(INFO) << "Received request for starting application at " << url;
    ledger_->NewPage([this, callback, url](
        ledger::Status status,
        mojo::InterfaceHandle<ledger::Page> session_page) {
      auto story_id = GenerateNewStoryId(10);
      session_page_map_[story_id].Bind(std::move(session_page));
      session_page_map_[story_id]->GetId(
          [this, callback, url, story_id](mojo::Array<uint8_t> id) {
            mojo::InterfaceHandle<Story> story;

            mojo::StructPtr<StoryInfo> info = StoryInfo::New();
            info->url = url;
            info->session_page_id = std::move(id);
            info->is_running = false;

            auto story_state = new StoryImpl(std::move(info), this, shell_,
                                             mojo::GetProxy(&story));
            story_ids_.insert(story_id);
            story_state_to_id_.emplace(story_state, story_id);
            story_id_to_state_.emplace(story_id, story_state);

            story_id_to_state_[story_id]->RunStory(
                std::move(session_page_map_[story_id]));
            callback.Run(story.Pass());
          });
    });
  }

  // |StoryProvider| override.
  // TODO(alhaad): Complete the implementation once
  // https://github.com/domokit/mojo/issues/818. is fixed.
  void PreviousStories(const PreviousStoriesCallback& callback) override {
    root_page_->GetSnapshot(
        [this, callback](ledger::Status status,
                         mojo::InterfaceHandle<ledger::PageSnapshot> snapshot) {
          callback.Run(nullptr);
        });
  }

  // Generates a unique randomly generated string of |length| size to be used
  // as a story id.
  std::string GenerateNewStoryId(size_t length) {
    auto randchar = []() -> char {
      const char charset[] =
          "0123456789"
          "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
          "abcdefghijklmnopqrstuvwxyz";
      const size_t max_index = (sizeof(charset) - 1);
      return charset[rand() % max_index];
    };
    std::string id(length, 0);
    std::generate_n(id.begin(), length, randchar);

    if (story_ids_.count(id) == 1) {
      return GenerateNewStoryId(length);
    }
    return id;
  }

  mojo::Shell* shell_;
  mojo::StrongBinding<StoryProvider> binding_;
  mojo::InterfacePtr<ledger::Ledger> ledger_;

  mojo::InterfacePtr<ledger::Page> root_page_;

  std::unordered_map<StoryState*, std::string> story_state_to_id_;
  std::unordered_map<std::string, StoryState*> story_id_to_state_;
  std::unordered_set<std::string> story_ids_;

  std::unordered_map<std::string, mojo::InterfacePtr<ledger::Page>>
      session_page_map_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryProviderImpl);
};

class StoryManagerImpl : public StoryManager {
 public:
  explicit StoryManagerImpl(mojo::Shell* shell,
                            mojo::InterfaceRequest<StoryManager> request)
      : shell_(shell), binding_(this, std::move(request)) {}
  ~StoryManagerImpl() override {}

 private:
  void Launch(mojo::StructPtr<ledger::Identity> identity,
              const LaunchCallback& callback) override {
    FTL_LOG(INFO) << "story_manager::Launch received.";

    // Establish connection with Ledger.
    mojo::ConnectToService(shell_, "mojo:ledger",
                           mojo::GetProxy(&ledger_factory_));
    ledger_factory_->GetLedger(
        std::move(identity),
        [this, callback](ledger::Status status,
                         mojo::InterfaceHandle<ledger::Ledger> ledger) {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "story-manager's connection to ledger failed.";
            callback.Run(false);
            return;
          }
          callback.Run(true);
          StartUserShell(std::move(ledger));
        });
  }

  // Run the User shell and provide it the |StoryProvider| interface.
  void StartUserShell(mojo::InterfaceHandle<ledger::Ledger> ledger) {
    mojo::ConnectToService(shell_, "mojo:dummy_user_shell",
                           mojo::GetProxy(&user_shell_));
    mojo::InterfaceHandle<StoryProvider> service;
    new StoryProviderImpl(
        shell_, mojo::InterfacePtr<ledger::Ledger>::Create(std::move(ledger)),
        &service);
    user_shell_->SetStoryProvider(std::move(service));
  }

  mojo::Shell* shell_;
  mojo::StrongBinding<StoryManager> binding_;

  mojo::InterfacePtr<UserShell> user_shell_;

  mojo::InterfacePtr<ledger::LedgerFactory> ledger_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryManagerImpl);
};

class StoryManagerApp : public mojo::ApplicationImplBase {
 public:
  StoryManagerApp() {}
  ~StoryManagerApp() override {}

 private:
  void OnInitialize() override { FTL_LOG(INFO) << "story-manager init"; }

  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override {
    // Register |StoryManager| implementation.
    service_provider_impl->AddService<StoryManager>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<StoryManager> launcher_request) {
          new StoryManagerImpl(shell(), std::move(launcher_request));
        });
    return true;
  }

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryManagerApp);
};

}  // namespace story_manager

MojoResult MojoMain(MojoHandle application_request) {
  story_manager::StoryManagerApp app;
  return mojo::RunApplication(application_request, &app);
}
