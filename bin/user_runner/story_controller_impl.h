// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_USER_RUNNER_STORY_CONTROLLER_IMPL_H_
#define APPS_MODULAR_SRC_USER_RUNNER_STORY_CONTROLLER_IMPL_H_

#include <memory>
#include <vector>

#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/services/application/application_launcher.fidl.h"
#include "apps/modular/services/story/module_controller.fidl.h"
#include "apps/modular/services/story/story.fidl.h"
#include "apps/modular/services/story/story_runner.fidl.h"
#include "apps/modular/services/user/story_data.fidl.h"
#include "apps/modular/services/user/story_provider.fidl.h"
#include "apps/modular/services/user/user_runner.fidl.h"
#include "apps/modular/src/user_runner/story_storage_impl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace modular {
class ApplicationContext;
class StoryProviderImpl;

class StoryControllerImpl : public StoryController, public ModuleWatcher {
 public:
  StoryControllerImpl(StoryDataPtr story_data,
                      StoryProviderImpl* story_provider_impl);
  ~StoryControllerImpl() override = default;

  // Used by StoryProviderImpl.
  void Connect(fidl::InterfaceRequest<StoryController> request);
  void StopForDelete(const StopCallback& callback);
  void AddLinkDataAndSync(
      const fidl::String& json, const std::function<void()>& callback);
  bool IsActive();  // Has story or pending stop or start requests?
  size_t bindings_size() const { return bindings_.size(); }

 private:
  // |StoryController|
  void GetInfo(const GetInfoCallback& callback) override;
  void SetInfoExtra(const fidl::String& name,
                    const fidl::String& value,
                    const SetInfoExtraCallback& callback) override;
  void Start(fidl::InterfaceRequest<mozart::ViewOwner> request) override;
  void GetLink(fidl::InterfaceRequest<Link> request) override;
  void Stop(const StopCallback& callback) override;
  void Watch(fidl::InterfaceHandle<StoryWatcher> watcher) override;

  // |ModuleWatcher|
  void OnStateChange(ModuleState new_state) override;

  void WriteStoryData(std::function<void()> callback);
  void NotifyStateChange();

  // Starts the StoryRunner instance to run the story in.
  void StartStoryRunner();

  // Starts the Story instance for the given story.
  void StartStory(fidl::InterfaceRequest<mozart::ViewOwner> request);

  // Resets the state of the story controller such that Start() can be
  // called again. This does not reset deleted_; once a
  // StoryController instance received StopForDelete(), it cannot be
  // reused anymore, and client connections will all be closed.
  void Reset();

  // The state of a Story and the context to obtain it from and
  // persist it to.
  StoryDataPtr story_data_;
  StoryProviderImpl* const story_provider_impl_;
  std::shared_ptr<StoryStorageImpl::Storage> storage_;
  std::unique_ptr<StoryStorageImpl> story_storage_impl_;

  // Implement the primary service provided here: StoryController.
  fidl::BindingSet<StoryController> bindings_;
  fidl::InterfacePtrSet<StoryWatcher> watchers_;

  // These 5 things are needed to hold on to a running story. They are
  // the ones that get reset on Stop()/Reset().
  StoryRunnerPtr story_runner_;
  StoryPtr story_;
  LinkPtr root_;
  ModuleControllerPtr module_;
  fidl::Binding<ModuleWatcher> module_watcher_binding_;

  // These fields hold state related to asynchronously completing operations.
  bool deleted_{};
  std::vector<std::function<void()>> stop_requests_;
  fidl::InterfaceRequest<mozart::ViewOwner> start_request_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryControllerImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_USER_RUNNER_STORY_IMPL_H_
