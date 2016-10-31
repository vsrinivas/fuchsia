// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_STORY_MANAGER_STORY_PROVIDER_STATE_H_
#define APPS_MODULAR_STORY_MANAGER_STORY_PROVIDER_STATE_H_

#include <unordered_map>
#include <unordered_set>

#include "apps/modular/services/user/user_runner.mojom.h"
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
  ~StoryProviderImpl() override;

  // Used to resume a story. Fetches the Session Page associated with
  // |story_impl| and calls |RunStory|. Does not take ownership of
  // |story_impl|.
  void ResumeStory(
      StoryImpl* story_impl,
      mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request);

  // Commits story meta-data to the ledger. Used after calling |Stop|
  // or when the |Story| pipe is closed. Does not take ownership of
  // |story_impl|.
  void CommitStory(StoryImpl* story_impl);

  // Removes all the in-memory data associated with |story_impl| from
  // |StoryProviderImpl|. Does not take ownership of |story_impl|.
  virtual void RemoveStory(StoryImpl* story_impl);

 private:
  // |StoryProvider| override.
  void CreateStory(const mojo::String& url,
                   mojo::InterfaceRequest<Story> story_request) override;

  // |StoryProvider| override.
  void PreviousStories(const PreviousStoriesCallback& callback) override;

  mojo::InterfacePtr<mojo::ApplicationConnector> app_connector_;
  mojo::StrongBinding<StoryProvider> binding_;
  mojo::InterfacePtr<ledger::Ledger> ledger_;

  mojo::InterfacePtr<ledger::Page> root_page_;

  std::unordered_map<StoryImpl*, std::string> story_impl_to_id_;
  std::unordered_map<std::string, StoryImpl*> story_id_to_impl_;
  std::unordered_set<std::string> story_ids_;

  std::unordered_map<std::string, mojo::InterfacePtr<ledger::Page>>
      session_page_map_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryProviderImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_STORY_MANAGER_STORY_PROVIDER_STATE_H_
