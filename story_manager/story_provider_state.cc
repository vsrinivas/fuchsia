// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/story_manager/story_provider_state.h"

#include "apps/modular/story_manager/story_state.h"
#include "lib/ftl/functional/make_copyable.h"
#include "mojo/public/cpp/bindings/array.h"

namespace modular {
namespace {

// A utility method to convert a string key into a byte mojo array.
mojo::Array<uint8_t> KeyToByteArray(const std::string& key) {
  mojo::Array<uint8_t> array = mojo::Array<uint8_t>::New(key.length());
  for (size_t i = 0; i < key.length(); i++) {
    array[i] = static_cast<uint8_t>(key[i]);
  }
  return array;
}

}  // namespace

// TODO(alhaad): The current implementation makes no use of |PageWatcher| and
// assumes that only one device can access a user's ledger. Re-visit this
// assumption.
StoryProviderState::StoryProviderState(
    mojo::Shell* shell,
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

StoryProviderState::~StoryProviderState() {}

void StoryProviderState::ResumeStoryState(
    StoryState* story_state,
    mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  auto info = story_state->GetStoryInfo();
  ledger_->GetPage(
      std::move(info->session_page_id),
      ftl::MakeCopyable([
        story_state, request = std::move(view_owner_request)
      ](ledger::Status status,
        mojo::InterfaceHandle<ledger::Page> session_page) mutable {
        story_state->RunStory(
            mojo::InterfacePtr<ledger::Page>::Create(std::move(session_page)),
            std::move(request));
      }));
}

void StoryProviderState::CommitStoryState(StoryState* story_state) {
  auto info = story_state->GetStoryInfo();
  auto size = info->GetSerializedSize();
  mojo::Array<uint8_t> value = mojo::Array<uint8_t>::New(size);
  info->Serialize(value.data(), size);
  auto story_id = story_state_to_id_[story_state];
  root_page_->PutWithPriority(KeyToByteArray(story_id), std::move(value),
                              ledger::Priority::EAGER,
                              [](ledger::Status status) {});
}

void StoryProviderState::RemoveStoryState(StoryState* story_state) {
  auto story_id = story_state_to_id_[story_state];
  story_state_to_id_.erase(story_state);
  story_id_to_state_.erase(story_id);
  story_ids_.erase(story_id);
}

void StoryProviderState::CreateStory(const mojo::String& url,
                                     const CreateStoryCallback& callback) {
  // TODO(alhaad): Creating multiple stories can only work after
  // https://fuchsia-review.googlesource.com/#/c/8941/ has landed.
  FTL_LOG(INFO) << "StoryProviderState::StartNewStory " << url;
  ledger_->NewPage([this, callback, url](
      ledger::Status status, mojo::InterfaceHandle<ledger::Page> session_page) {
    auto story_id = GenerateNewStoryId(10);
    session_page_map_[story_id].Bind(std::move(session_page));
    session_page_map_[story_id]->GetId(
        [this, callback, url, story_id](mojo::Array<uint8_t> id) {
          mojo::StructPtr<StoryInfo> info = StoryInfo::New();
          info->url = url;
          info->session_page_id = std::move(id);
          info->is_running = false;

          mojo::InterfaceHandle<Story> story;
          StoryState* story_state =
              new StoryState(std::move(info), this, shell_, GetProxy(&story));
          story_ids_.insert(story_id);
          story_state_to_id_.emplace(story_state, story_id);
          story_id_to_state_.emplace(story_id, story_state);

          callback.Run(story.Pass());
        });
  });
}

void StoryProviderState::PreviousStories(
    const PreviousStoriesCallback& callback) {
  root_page_->GetSnapshot(
      [this, callback](ledger::Status status,
                       mojo::InterfaceHandle<ledger::PageSnapshot> snapshot) {
        callback.Run(nullptr);
      });
}

std::string StoryProviderState::GenerateNewStoryId(size_t length) {
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

}  // namespace modular
