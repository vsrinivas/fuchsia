// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_STORY_MANAGER_STORY_PROVIDER_STATE_H_
#define APPS_MODULAR_STORY_MANAGER_STORY_PROVIDER_STATE_H_

#include <unordered_map>
#include <unordered_set>

#include "apps/modular/story_manager/story_manager.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/string.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/interfaces/application/application_connector.mojom.h"

namespace modular {
class StoryState;

class StoryProviderState : public StoryProvider {
 public:
  StoryProviderState(
      mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
      mojo::InterfacePtr<ledger::Ledger> ledger,
      mojo::InterfaceHandle<StoryProvider>* service);
  ~StoryProviderState() override;

  // Used to resume a story. Fetches the Session Page associated with
  // |story_state| and calls |RunStory|. This does not take ownership of
  // |story_state|.
  void ResumeStoryState(
      StoryState* story_state,
      mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request);

  // Commits story meta-data to the ledger. This is used after calling
  // |Stop| or when the |Story| pipe is close. This does not take ownership
  // of |story_state|.
  void CommitStoryState(StoryState* story_state);

  // Removes all the in-memory data structures from |StoryProviderState|
  // associated with |story_state|. This does not take ownership of
  // |story_state|.
  virtual void RemoveStoryState(StoryState* story_state);

 private:
  // |StoryProvider| override.
  void CreateStory(const mojo::String& url,
                   mojo::InterfaceRequest<Story> request) override;

  // |StoryProvider| override.
  void PreviousStories(const PreviousStoriesCallback& callback) override;

  // Generates a unique randomly generated string of |length| size to be used
  // as a story id.
  std::string GenerateNewStoryId(size_t length);

  mojo::InterfacePtr<mojo::ApplicationConnector> app_connector_;
  mojo::StrongBinding<StoryProvider> binding_;
  mojo::InterfacePtr<ledger::Ledger> ledger_;

  mojo::InterfacePtr<ledger::Page> root_page_;

  std::unordered_map<StoryState*, std::string> story_state_to_id_;
  std::unordered_map<std::string, StoryState*> story_id_to_state_;
  std::unordered_set<std::string> story_ids_;

  std::unordered_map<std::string, mojo::InterfacePtr<ledger::Page>>
      session_page_map_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryProviderState);
};

}  // namespace modular

#endif  // APPS_MODULAR_STORY_MANAGER_STORY_PROVIDER_STATE_H_
