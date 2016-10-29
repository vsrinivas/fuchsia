// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_STORY_MANAGER_STORY_STATE_H_
#define APPS_MODULAR_STORY_MANAGER_STORY_STATE_H_

#include "apps/modular/services/user/user_runner.mojom.h"
#include "apps/modular/services/story/story_runner.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/interfaces/application/application_connector.mojom.h"
#include "mojo/public/interfaces/application/shell.mojom.h"

namespace modular {
class StoryProviderState;

class StoryState : public Story, public ModuleWatcher {
 public:
  StoryState(mojo::StructPtr<StoryInfo> story_info,
             StoryProviderState* story_provider_state,
             mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
             mojo::InterfaceRequest<Story> request);
  ~StoryState() override;

  mojo::StructPtr<StoryInfo> GetStoryInfo() const;
  // Runs this story. If |session_page| is empty, we are effectively starting
  // a new session, else we are re-inflating an existing session.
  // This is responsible for commiting data to |session_page|.
  // |StoryState| override.
  void RunStory(mojo::InterfacePtr<ledger::Page> session_page,
                mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request);

 private:
  // |ModuleWatcher| override.
  void Done() override;

  // |Story| override.
  void GetInfo(const GetInfoCallback& callback) override;

  // |Story| override.
  void Start(
      mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request) override;

  // |Story| override.
  void Stop() override;

  mojo::StructPtr<StoryInfo> story_info_;
  StoryProviderState* story_provider_state_;
  mojo::InterfacePtr<mojo::ApplicationConnector> app_connector_;
  mojo::StrongBinding<Story> binding_;
  mojo::Binding<ModuleWatcher> module_watcher_binding_;

  mojo::InterfacePtr<StoryRunner> runner_;
  mojo::InterfacePtr<Session> session_;
  mojo::InterfacePtr<ModuleController> module_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryState);
};

}  // namespace modular

#endif  // APPS_MODULAR_STORY_MANAGER_STORY_STATE_H_
