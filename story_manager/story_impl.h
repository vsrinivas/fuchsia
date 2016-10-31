// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_STORY_MANAGER_STORY_STATE_H_
#define APPS_MODULAR_STORY_MANAGER_STORY_STATE_H_

#include "apps/modular/services/story/story_runner.mojom.h"
#include "apps/modular/services/user/user_runner.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/interfaces/application/application_connector.mojom.h"
#include "mojo/public/interfaces/application/shell.mojom.h"

namespace modular {
class StoryProviderImpl;

class StoryImpl : public Story, public ModuleWatcher {
 public:
  static StoryImpl* New(
      mojo::StructPtr<StoryInfo> story_info,
      StoryProviderImpl* story_provider_impl,
      mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
      mojo::InterfaceRequest<Story> story_request) {
    return new StoryImpl(std::move(story_info), story_provider_impl,
                         std::move(app_connector), std::move(story_request));
  }
  ~StoryImpl() override;

  mojo::StructPtr<StoryInfo> GetStoryInfo() const;

  // Runs this story. If |session_page| is empty, we are effectively
  // starting a new session, else we are re-inflating an existing
  // session. Will write session info data to |session_page|.
  void RunStory(mojo::InterfacePtr<ledger::Page> session_page,
                mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request);

 private:  // factory support
  // Constructor is private to ensure (by way of New()) that instances
  // are created always with new. This is necessary because Done()
  // calls delete this.
  StoryImpl(mojo::StructPtr<StoryInfo> story_info,
            StoryProviderImpl* story_provider_impl,
            mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
            mojo::InterfaceRequest<Story> story_request);

 private:  // virtual method implementations
  // |ModuleWatcher| override.
  void Done() override;

  // |Story| override.
  void GetInfo(const GetInfoCallback& callback) override;

  // |Story| override.
  void Start(
      mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request) override;

  // |Story| override.
  void Stop() override;

 private:
  mojo::StructPtr<StoryInfo> story_info_;
  StoryProviderImpl* const story_provider_impl_;
  mojo::InterfacePtr<mojo::ApplicationConnector> app_connector_;
  mojo::StrongBinding<Story> binding_;
  mojo::Binding<ModuleWatcher> module_watcher_binding_;

  mojo::InterfacePtr<StoryRunner> runner_;
  mojo::InterfacePtr<Session> session_;
  mojo::InterfacePtr<ModuleController> module_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_STORY_MANAGER_STORY_STATE_H_
