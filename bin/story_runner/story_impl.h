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
#include <vector>

#include "apps/modular/services/application/application_launcher.fidl.h"
#include "apps/modular/services/story/module.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "apps/modular/services/story/resolver.fidl.h"
#include "apps/modular/services/story/story.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/ftl/macros.h"

namespace ledger {
class LedgerRepository;
}

namespace modular {

class ApplicationContext;
class LinkImpl;
class ModuleControllerImpl;
class StoryConnection;
class StoryImpl;
class StoryPage;
class StoryStorageImpl;

// StoryConnection keeps a single connection from a module instance in the
// story to a StoryImpl. This way, requests that the module makes on
// its Story handle can be associated with the Module instance.
class StoryConnection : public Story {
 public:
  // If this Story handle is for a module started by
  // Story.StartModule(), there is also a module_controller_impl. If
  // requested through StoryRunner.GetStory(), module_controller_impl
  // is nullptr.
  StoryConnection(StoryImpl* story_impl,
                  const std::string& module_url,
                  ModuleControllerImpl* module_controller_impl,
                  fidl::InterfaceRequest<Story> story);

  ~StoryConnection() override = default;

 private:
  // |Story|
  void CreateLink(const fidl::String& name,
                  fidl::InterfaceRequest<Link> link) override;
  void StartModule(
      const fidl::String& query,
      fidl::InterfaceHandle<Link> link,
      fidl::InterfaceHandle<ServiceProvider> outgoing_services,
      fidl::InterfaceRequest<ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner) override;
  void GetLedger(fidl::InterfaceRequest<ledger::Ledger> module_ledger,
                 const GetLedgerCallback& result) override;
  void Ready() override;
  void Done() override;

  // Not owned.
  StoryImpl* const story_impl_;

  // This ID is used to namespace a module's ledger.
  std::string module_url_;

  // Not owned. Used to notify module watchers and request tear down.
  ModuleControllerImpl* const module_controller_impl_;
  fidl::Binding<Story> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryConnection);
};

// The actual implementation of the Story service. Called from
// StoryConnection above.
class StoryImpl {
 public:
  StoryImpl(ApplicationLauncher* launcher,
            fidl::InterfaceHandle<Resolver> resolver,
            StoryStorageImpl* story_storage,
            ledger::LedgerRepository* ledger_repository);

  ~StoryImpl();

  // These methods are called by StoryConnection.
  void CreateLink(const fidl::String& name, fidl::InterfaceRequest<Link> request);
  void StartModule(const fidl::String& query,
                   fidl::InterfaceHandle<Link> link,
                   fidl::InterfaceHandle<ServiceProvider> outgoing_services,
                   fidl::InterfaceRequest<ServiceProvider> incoming_services,
                   fidl::InterfaceRequest<ModuleController> module_controller,
                   fidl::InterfaceRequest<mozart::ViewOwner> view_owner);
  void GetLedger(const std::string& module_name,
                 fidl::InterfaceRequest<ledger::Ledger> module_ledger,
                 const std::function<void(ledger::Status)>& result);
  // Releases ownership of |controller|.
  void ReleaseModule(ModuleControllerImpl* controller);

  // Used by StoryController.
  void Stop(const std::function<void()>& done);

 private:
  void DisposeLink(LinkImpl* link);

  // Phases of Stop(), broken out into separate methods.
  void StopModules();
  void StopLinks();

  struct Connection {
    std::unique_ptr<StoryConnection> story_connection;
    std::unique_ptr<ModuleControllerImpl> module_controller_impl;
  };

  ApplicationLauncher* const launcher_;
  fidl::InterfacePtr<Resolver> resolver_;
  StoryStorageImpl* const story_storage_;
  ledger::LedgerRepository* const ledger_repository_;
  std::vector<Connection> connections_;

  std::vector<std::unique_ptr<LinkImpl>> links_;

  // Callbacks for teardown requests in flight. This batches up
  // concurrent Stop() requests (which may arise because the teardown
  // sequence is asynchronous) into a single tear down sequence.
  std::vector<std::function<void()>> teardown_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_STORY_IMPL_H_
