// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/story_manager/story_provider_impl.h"

#include "apps/modular/story_manager/story_impl.h"
#include "lib/ftl/functional/make_copyable.h"
#include "mojo/public/cpp/application/connect.h"
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

// Generates a unique randomly generated string of |length| size to be
// used as a story id.
std::string MakeStoryId(const std::unordered_set<std::string>& story_ids,
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

  if (story_ids.find(id) != story_ids.end()) {
    return MakeStoryId(story_ids, length);
  }

  return id;
}

}  // namespace

// TODO(alhaad): The current implementation makes no use of |PageWatcher| and
// assumes that only one device can access a user's ledger. Re-visit this
// assumption.
StoryProviderImpl::StoryProviderImpl(
    mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
    mojo::InterfaceHandle<ledger::Ledger> ledger,
    mojo::InterfaceRequest<StoryProvider> story_provider_request)
    : binding_(this, std::move(story_provider_request)) {
  app_connector_.Bind(std::move(app_connector));
  ledger_.Bind(std::move(ledger));

  ledger_->GetRootPage(GetProxy(&root_page_), [](ledger::Status status) {
    if (status != ledger::Status::OK) {
      FTL_NOTREACHED() << "Ledger did not return root page. Error: " << status;
    }
  });
}

StoryProviderImpl::~StoryProviderImpl() {}

void StoryProviderImpl::ResumeStory(
    StoryImpl* const story_impl,
    mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  mojo::StructPtr<StoryInfo> story_info = story_impl->GetStoryInfo();
  mojo::InterfacePtr<ledger::Page> session_page;
  ledger_->GetPage(std::move(story_info->session_page_id),
                   GetProxy(&session_page), [](ledger::Status status) {
                     if (status != ledger::Status::OK) {
                       FTL_NOTREACHED() << "Ledger did not return a page to "
                                           "resume the story. Error: "
                                        << status;
                     }
                   });
  story_impl->RunStory(
      mojo::InterfacePtr<ledger::Page>::Create(std::move(session_page)),
      std::move(view_owner_request));
}

void StoryProviderImpl::CommitStory(StoryImpl* const story_impl) {
  mojo::StructPtr<StoryInfo> story_info = story_impl->GetStoryInfo();
  const size_t size = story_info->GetSerializedSize();

  mojo::Array<uint8_t> value = mojo::Array<uint8_t>::New(size);
  story_info->Serialize(value.data(), size);

  const std::string& story_id = story_impl_to_id_[story_impl];
  root_page_->PutWithPriority(KeyToByteArray(story_id), std::move(value),
                              ledger::Priority::EAGER,
                              [](ledger::Status status) {});
}

void StoryProviderImpl::RemoveStory(StoryImpl* const story_impl) {
  const std::string story_id = story_impl_to_id_[story_impl];

  story_impl_to_id_.erase(story_impl);
  story_id_to_impl_.erase(story_id);
  story_ids_.erase(story_id);
}

void StoryProviderImpl::CreateStory(
    const mojo::String& url,
    mojo::InterfaceRequest<Story> story_request) {
  // TODO(alhaad): Creating multiple stories can only work after
  // https://fuchsia-review.googlesource.com/#/c/8941/ has landed.
  FTL_LOG(INFO) << "StoryProviderImpl::CreateStory() " << url;
  // TODO(mesch): This is sloppy: We check the new story ID here
  // against story_ids_, but insert it only asynchoronously
  // below. In principle a second request for CreateStory()
  // could create the same story ID again. We should not use
  // random IDs anyway.
  const std::string story_id = MakeStoryId(story_ids_, 10);
  ledger_->NewPage(
      GetProxy(&session_page_map_[story_id]), [](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_NOTREACHED() << "Ledger did not create a new page. Error: "
                           << status;
        }
      });
  session_page_map_[story_id]->GetId(ftl::MakeCopyable([
    this, story_request = std::move(story_request), url, story_id
  ](mojo::Array<uint8_t> session_page_id) mutable {
    mojo::StructPtr<StoryInfo> story_info = StoryInfo::New();
    story_info->url = url;
    story_info->session_page_id = std::move(session_page_id);
    story_info->is_running = false;

    StoryImpl* const story_impl = StoryImpl::New(
        std::move(story_info), this,
        mojo::DuplicateApplicationConnector(app_connector_.get()),
        std::move(story_request));
    story_ids_.insert(story_id);
    story_impl_to_id_.emplace(story_impl, story_id);
    story_id_to_impl_.emplace(story_id, story_impl);
  }));
}

void StoryProviderImpl::PreviousStories(
    const PreviousStoriesCallback& callback) {
  mojo::InterfacePtr<ledger::PageSnapshot> snapshot;
  root_page_->GetSnapshot(
      GetProxy(&snapshot),
      [this, callback](ledger::Status status) { callback.Run(nullptr); });
}

}  // namespace modular
