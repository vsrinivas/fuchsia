// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The Story service is the context in which a story executes. It
// starts modules and provides them with a handle to itself, so they
// can start more modules. It also serves as the factory for Link
// instances, which are used to share data between modules.

#ifndef APPS_MODULAR_SRC_STORY_RUNNER_STORY_IMPL_H_
#define APPS_MODULAR_SRC_STORY_RUNNER_STORY_IMPL_H_

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "apps/modular/services/document/document.fidl.h"
#include "apps/modular/document_editor/document_editor.h"
#include "apps/modular/services/story/resolver.fidl.h"
#include "apps/modular/services/story/story.fidl.h"
#include "apps/modular/mojo/strong_binding.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/ftl/macros.h"

namespace modular {

class ApplicationContext;
class StoryHost;
class StoryImpl;
class StoryPage;

// Implements the ModuleController interface, which is passed back to
// the client that requested a module to be started this StoryHost
// is passed to on Initialize(). One instance of ModuleControllerImpl is
// associated with each StoryHost instance.
class ModuleControllerImpl : public ModuleController {
 public:
  ModuleControllerImpl(
      StoryHost* story,
      fidl::InterfacePtr<Module> module,
      fidl::InterfaceRequest<ModuleController> module_controller);
  ~ModuleControllerImpl();

  // Implements ModuleController.
  void Watch(fidl::InterfaceHandle<ModuleWatcher> watcher) override;

  // Called by StoryHost. Closes the module handle and notifies
  // watchers.
  void Done();

 private:
  StoryHost* const story_;
  StrongBinding<ModuleController> binding_;
  fidl::InterfacePtr<Module> module_;
  std::vector<fidl::InterfacePtr<ModuleWatcher>> watchers_;
  FTL_DISALLOW_COPY_AND_ASSIGN(ModuleControllerImpl);
};

// StoryHost keeps a single connection from a client (i.e., a module
// instance in the same story) to a StoryImpl together with
// pointers to all links created and modules started through this
// connection. This allows to persist and recreate the story state
// correctly.
class StoryHost : public Story {
 public:
  // Primary story host created when StoryImpl is created from story
  // manager.
  StoryHost(StoryImpl* impl, fidl::InterfaceRequest<Story> story);

  // Non-primary story host created for the module started by StartModule().
  StoryHost(StoryImpl* impl,
            fidl::InterfaceRequest<Story> story,
            fidl::InterfacePtr<Module> module,
            fidl::InterfaceRequest<ModuleController> module_controller);
  ~StoryHost() override;

  // Implements Story interface. Forwards to StoryImpl.
  void CreateLink(const fidl::String& name,
                  fidl::InterfaceRequest<Link> link) override;
  void StartModule(
      const fidl::String& query,
      fidl::InterfaceHandle<Link> link,
      fidl::InterfaceRequest<ModuleController> module_controller,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner) override;
  void Done() override;

  // Called by ModuleControllerImpl.
  void Add(ModuleControllerImpl* module_controller);
  void Remove(ModuleControllerImpl* module_controller);

 private:
  StoryImpl* const impl_;
  StrongBinding<Story> binding_;
  ModuleControllerImpl* module_controller_;
  const bool primary_;
  FTL_DISALLOW_COPY_AND_ASSIGN(StoryHost);
};

// The actual implementation of the Story service. Called from
// StoryHost above.
class StoryImpl {
 public:
  StoryImpl(std::shared_ptr<ApplicationContext> application_context,
            fidl::InterfaceHandle<Resolver> resolver,
            fidl::InterfaceHandle<StoryStorage> story_storage,
            fidl::InterfaceRequest<Story> story_request);
  ~StoryImpl();

  // These methods are called by StoryHost.
  void Add(StoryHost* client);
  void Remove(StoryHost* client);
  void CreateLink(const fidl::String& name, fidl::InterfaceRequest<Link> link);
  void StartModule(const fidl::String& query,
                   fidl::InterfaceHandle<Link> link,
                   fidl::InterfaceRequest<ModuleController> module_controller,
                   fidl::InterfaceRequest<mozart::ViewOwner> view_owner);

 private:
  std::shared_ptr<ApplicationContext> application_context_;
  fidl::InterfacePtr<Resolver> resolver_;
  std::shared_ptr<StoryPage> page_;
  std::vector<StoryHost*> clients_;
  FTL_DISALLOW_COPY_AND_ASSIGN(StoryImpl);
};

// Shared owner of the connection to the ledger page. Shared between
// the StoryImpl, and all LinkImpls, so the connection is around
// until all Links are closed when the story shuts down.
class StoryPage {
 public:
  StoryPage(fidl::InterfaceHandle<StoryStorage> story_storage);
  ~StoryPage();

  void Init(std::function<void()> done);

  // These methods are called by LinkImpl.
  void MaybeReadLink(const fidl::String& name, MojoDocMap* data);
  void WriteLink(const fidl::String& name, const MojoDocMap& data);

 private:
  fidl::InterfacePtr<StoryStorage> story_storage_;
  fidl::StructPtr<StoryData> data_;
  fidl::Array<uint8_t> id_;  // logging only

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryPage);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_STORY_IMPL_H_
