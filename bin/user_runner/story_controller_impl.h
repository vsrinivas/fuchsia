// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_USER_RUNNER_STORY_CONTROLLER_IMPL_H_
#define APPS_MODULAR_SRC_USER_RUNNER_STORY_CONTROLLER_IMPL_H_

#include <memory>
#include <vector>

#include "apps/modular/lib/document_editor/document_editor.h"
#include "apps/modular/lib/fidl/strong_binding.h"
#include "apps/modular/services/application/application_launcher.fidl.h"
#include "apps/modular/services/story/story_runner.fidl.h"
#include "apps/modular/services/user/user_runner.fidl.h"
#include "apps/modular/src/user_runner/story_storage_impl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace modular {
class ApplicationContext;
class StoryProviderImpl;

class StoryControllerImpl : public StoryController,
                            public ModuleWatcher,
                            public LinkWatcher {
 public:
  static StoryControllerImpl* New(
      StoryInfoPtr story_info,
      StoryProviderImpl* story_provider_impl,
      ApplicationLauncherPtr launcher,
      fidl::InterfaceRequest<StoryController> story_controller_request) {
    return new StoryControllerImpl(std::move(story_info), story_provider_impl,
                                   std::move(launcher),
                                   std::move(story_controller_request));
  }

  ~StoryControllerImpl() override;

 private:  // factory support
  // Constructor is private to ensure (by way of New()) that instances
  // are created always with new. This is necessary because Done()
  // calls delete this.
  StoryControllerImpl(
      StoryInfoPtr story_info,
      StoryProviderImpl* story_provider_impl,
      ApplicationLauncherPtr launcher,
      fidl::InterfaceRequest<StoryController> story_controller_request);

 private:  // virtual method implementations
  // |Story|
  void GetInfo(const GetInfoCallback& callback) override;

  // |Story|
  void Start(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) override;

  // |Story|
  void Stop() override;

  // |Story|
  void Watch(fidl::InterfaceHandle<StoryWatcher> story_watcher) override;

  // |ModuleWatcher|
  void Done() override;

  // |LinkWatcher|
  void Notify(FidlDocMap docs) override;

 private:
  void NotifyStoryWatchers(void (StoryWatcher::*method)());

  // Starts the StoryRunner instance for the given story.
  void StartStoryRunner(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request);

  // Tears down the currently used StoryRunner instance, if any.
  void TearDownStoryRunner();

  StoryInfoPtr story_info_;
  StoryProviderImpl* const story_provider_impl_;
  std::shared_ptr<StoryStorageImpl::Storage> storage_;
  ApplicationLauncherPtr launcher_;

  StrongBinding<StoryController> binding_;
  fidl::Binding<ModuleWatcher> module_watcher_binding_;
  fidl::Binding<LinkWatcher> link_changed_binding_;
  std::vector<StoryWatcherPtr> story_watchers_;
  StoryPtr story_;
  LinkPtr root_;
  ModuleControllerPtr module_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryControllerImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_USER_RUNNER_STORY_IMPL_H_
