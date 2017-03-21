// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The Story service is the context in which a story executes. It
// starts modules and provides them with a handle to itself, so they
// can start more modules. It also serves as the factory for Link
// instances, which are used to share data between modules.

#ifndef APPS_MODULAR_SRC_STORY_RUNNER_STORY_IMPL_H_
#define APPS_MODULAR_SRC_STORY_RUNNER_STORY_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include "application/services/application_controller.fidl.h"
#include "apps/modular/lib/fidl/scope.h"
#include "apps/modular/services/component/component_context.fidl.h"
#include "apps/modular/services/module/module.fidl.h"
#include "apps/modular/services/module/module_controller.fidl.h"
#include "apps/modular/services/story/story_controller.fidl.h"
#include "apps/modular/services/story/story_data.fidl.h"
#include "apps/modular/services/story/story_marker.fidl.h"
#include "apps/modular/services/story/story_shell.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/ftl/macros.h"

namespace modular {

class LinkImpl;
class ModuleControllerImpl;
class ModuleContextImpl;
class StoryImpl;
class StoryPage;
class StoryProviderImpl;
class StoryStorageImpl;

constexpr char kRootLink[] = "root";

// The actual implementation of the Story service. It also implements
// the StoryController service to give clients control over the Story
// instance.
class StoryImpl : public StoryController, StoryContext, ModuleWatcher {
 public:
  StoryImpl(StoryDataPtr story_data,
            StoryProviderImpl* const story_provider_impl);

  ~StoryImpl() override;

  // Called by ModuleContextImpl.
  void CreateLink(const fidl::String& name,
                  fidl::InterfaceRequest<Link> request);
  // Called by ModuleContextImpl.
  void StartModule(
      const fidl::String& query,
      fidl::InterfaceHandle<Link> link,
      fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner);
  // Called by ModuleContextImpl.
  void StartModuleInShell(
      const fidl::String& query,
      fidl::InterfaceHandle<Link> link,
      fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller);
  // Called by ModuleContextImpl.
  const std::string& GetStoryId();

  // Releases ownership of |controller|.
  void ReleaseModule(ModuleControllerImpl* controller);

  // Methods called by StoryProviderImpl.
  void Connect(fidl::InterfaceRequest<StoryController> request);
  void StopForDelete(const StopCallback& callback);
  void AddLinkDataAndSync(const fidl::String& json,
                          const std::function<void()>& callback);

 private:
  // |StoryController|
  void GetInfo(const GetInfoCallback& callback) override;
  void SetInfoExtra(const fidl::String& name,
                    const fidl::String& value,
                    const SetInfoExtraCallback& callback) override;
  void Start(fidl::InterfaceRequest<mozart::ViewOwner> request) override;
  void GetLink(fidl::InterfaceRequest<Link> request) override;
  void GetNamedLink(const fidl::String& name,
                    fidl::InterfaceRequest<Link> request) override;
  void Stop(const StopCallback& callback) override;
  void Watch(fidl::InterfaceHandle<StoryWatcher> watcher) override;
  void AddModule(const fidl::String& url,
                 const fidl::String& link_name) override;

  // Phases of Start() broken out into separate methods.
  void StartStoryShell(fidl::InterfaceRequest<mozart::ViewOwner> request);
  void StartRootModule(const fidl::String& url, const fidl::String& link_name);

  // Phases of Stop() broken out into separate methods.
  void StopModules();
  void StopStoryShell();
  void StopLinks();
  void StopFinish();

  // |ModuleWatcher|
  void OnStateChange(ModuleState new_state) override;

  // Misc internal helpers.
  void WriteStoryData(std::function<void()> callback);
  void NotifyStateChange();
  void DisposeLink(LinkImpl* link);
  LinkPtr& EnsureRoot();

  // The scope in which the modules within this story run.
  Scope story_scope_;

  // The state of a Story and the context to obtain it from and
  // persist it to.
  StoryDataPtr story_data_;
  StoryProviderImpl* const story_provider_impl_;
  std::unique_ptr<StoryStorageImpl> story_storage_impl_;

  // Implements the primary service provided here: StoryController.
  fidl::BindingSet<StoryController> bindings_;
  fidl::InterfacePtrSet<StoryWatcher> watchers_;

  // Everything for the story shell.
  app::ApplicationControllerPtr story_shell_controller_;
  StoryShellPtr story_shell_;
  fidl::Binding<StoryContext> story_context_binding_;

  // Needed to hold on to a running story. They get reset on Stop().
  LinkPtr root_;
  std::vector<ModuleControllerPtr> module_controllers_;
  fidl::BindingSet<ModuleWatcher> module_watcher_bindings_;

  // State related to asynchronously completing a Stop() operation.
  bool deleted_{};
  fidl::InterfaceRequest<mozart::ViewOwner> start_request_;
  std::vector<std::function<void()>> teardown_;

  // The ingredient parts of a story: Modules and Links. For each
  // Module, there is one Connection to it.
  struct Connection {
    std::unique_ptr<ModuleContextImpl> module_context_impl;
    std::unique_ptr<ModuleControllerImpl> module_controller_impl;
  };
  std::vector<Connection> connections_;
  std::vector<std::unique_ptr<LinkImpl>> links_;

  // A dummy service that allows applications that can run both as
  // modules in a story and standalone from the shell to determine
  // whether they are in a story. See story_marker.fidl for more
  // details.
  class StoryMarkerImpl : private StoryMarker {
   public:
    StoryMarkerImpl() = default;
    ~StoryMarkerImpl() override = default;

    void AddBinding(fidl::InterfaceRequest<StoryMarker> request) {
      bindings_.AddBinding(this, std::move(request));
    }

   private:
    fidl::BindingSet<StoryMarker> bindings_;
    FTL_DISALLOW_COPY_AND_ASSIGN(StoryMarkerImpl);
  };
  StoryMarkerImpl story_marker_impl_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_STORY_IMPL_H_
