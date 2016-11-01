// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_STORY_MANAGER_STORY_PROVIDER_STATE_H_
#define APPS_MODULAR_STORY_MANAGER_STORY_PROVIDER_STATE_H_

#include <unordered_map>
#include <unordered_set>

#include "apps/modular/services/user/user_runner.mojom.h"
#include "apps/modular/story_manager/transaction.h"
#include "apps/modular/story_manager/session_storage_impl.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/string.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/interfaces/application/application_connector.mojom.h"

namespace modular {
class StoryImpl;

class StoryProviderImpl : public StoryProvider {
 public:
  StoryProviderImpl(
      mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
      mojo::InterfaceHandle<ledger::Ledger> ledger,
      mojo::InterfaceRequest<StoryProvider> story_provider_request);

  ~StoryProviderImpl() override = default;

  // Obtains the StoryInfo for an existing story from the ledger.
  void GetStoryInfo(const mojo::String& session_id,
                    std::function<void(mojo::StructPtr<StoryInfo> story_info)>
                        story_info_callback);

  // Used to obtain a ledger page for the given session identified by
  // its ledger page ID.
  void GetSessionPage(
      mojo::Array<uint8_t> session_page_id,
      std::function<void(mojo::InterfaceHandle<ledger::Page> session_page)>
          session_page_callback);

  // Used by StoryImpl to write story meta-data to the ledger. Used
  // after calling |Stop| or when the |Story| pipe is closed.
  void WriteStoryInfo(mojo::StructPtr<StoryInfo> story_info);

  // Used by CreateStory() to write story meta-data to the ledger.
  void WriteStoryInfo(mojo::StructPtr<StoryInfo> story_info,
                      std::function<void()> done);

  // Used by StoryImpl.
  using Storage = SessionStorageImpl::Storage;
  std::shared_ptr<Storage> storage() { return storage_; }

 private:
  // |StoryProvider|
  void CreateStory(const mojo::String& url,
                   mojo::InterfaceRequest<Story> story_request) override;

  // |StoryProvider|
  void ResumeStoryById(const mojo::String& story_id,
                       mojo::InterfaceRequest<Story> story_request) override;

  // |StoryProvider|
  void ResumeStoryByInfo(mojo::StructPtr<StoryInfo> story_info,
                         mojo::InterfaceRequest<Story> story_request) override;

  // |StoryProvider|
  void PreviousStories(const PreviousStoriesCallback& callback) override;

  mojo::InterfacePtr<mojo::ApplicationConnector> app_connector_;
  mojo::StrongBinding<StoryProvider> binding_;
  mojo::InterfacePtr<ledger::Ledger> ledger_;

  std::unordered_set<std::string> story_ids_;
  std::unique_ptr<TransactionContainer> transaction_container_;
  std::shared_ptr<Storage> storage_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryProviderImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_STORY_MANAGER_STORY_PROVIDER_STATE_H_
