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

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/services/component/component_context.fidl.h"
#include "apps/modular/services/story/module.fidl.h"
#include "apps/modular/services/story/story_controller.fidl.h"
#include "apps/modular/services/story/story_data.fidl.h"
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
class StoryConnection;
class StoryImpl;
class StoryPage;
class StoryProviderImpl;
class StoryStorageImpl;

// The actual implementation of the Story service. It also implements
// the StoryController service to give clients control over the Story
// instance.
class StoryImpl : public StoryController, ModuleWatcher {
 public:
  StoryImpl(StoryDataPtr story_data,
            StoryProviderImpl* const story_provider_impl);

  ~StoryImpl() override;

  // Methods called by StoryConnection.
  void CreateLink(const fidl::String& name,
                  fidl::InterfaceRequest<Link> request);
  void StartModule(
      const fidl::String& query,
      fidl::InterfaceHandle<Link> link,
      fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner);
  void GetLedger(const std::string& module_url,
                 fidl::InterfaceRequest<ledger::Ledger> request,
                 const std::function<void(ledger::Status)>& result);
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
  void Stop(const StopCallback& callback) override;
  void Watch(fidl::InterfaceHandle<StoryWatcher> watcher) override;

  // Phases of Start() broken out into separate methods.
  void StartRootModule(fidl::InterfaceRequest<mozart::ViewOwner> request);

  // Phases of Stop() broken out into separate methods.
  void StopModules();
  void StopLinks();
  void StopFinish();

  // |ModuleWatcher|
  void OnStateChange(ModuleState new_state) override;

  // Misc internal helpers.
  void WriteStoryData(std::function<void()> callback);
  void NotifyStateChange();
  void DisposeLink(LinkImpl* link);
  LinkPtr& EnsureRoot();

  // The state of a Story and the context to obtain it from and
  // persist it to.
  StoryDataPtr story_data_;
  StoryProviderImpl* const story_provider_impl_;
  std::unique_ptr<StoryStorageImpl> story_storage_impl_;

  // Implements the primary service provided here: StoryController.
  fidl::BindingSet<StoryController> bindings_;
  fidl::InterfacePtrSet<StoryWatcher> watchers_;

  // Needed to hold on to a running story. They get reset on Stop().
  LinkPtr root_;
  ModuleControllerPtr module_;
  fidl::Binding<ModuleWatcher> module_watcher_binding_;

  // State related to asynchronously completing a Stop() operation.
  bool deleted_{};
  fidl::InterfaceRequest<mozart::ViewOwner> start_request_;
  std::vector<std::function<void()>> teardown_;

  // The ingredient parts of a story: Modules and Links. For each
  // Module, there is one Connection to it.
  struct Connection {
    std::unique_ptr<StoryConnection> story_connection;
    std::unique_ptr<ModuleControllerImpl> module_controller_impl;
  };
  std::vector<Connection> connections_;
  std::vector<std::unique_ptr<LinkImpl>> links_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_STORY_IMPL_H_
