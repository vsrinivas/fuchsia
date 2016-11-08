// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The Session is the context in which a story executes. It starts
// modules and provides them with a handle to itself, so they can
// start more modules. It also serves as the factory for Link
// instances, which are used to share data between modules.

#ifndef APPS_MODULAR_STORY_RUNNER_SESSION_H__
#define APPS_MODULAR_STORY_RUNNER_SESSION_H__

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "apps/modular/services/document/document.fidl.h"
#include "apps/modular/document_editor/document_editor.h"
#include "apps/modular/services/story/resolver.fidl.h"
#include "apps/modular/services/story/session.fidl.h"
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
class SessionHost;
class SessionImpl;
class SessionPage;

// Implements the ModuleController interface, which is passed back to
// the client that requested a module to be started this SessionHost
// is passed to on Initialize(). One instance of ModuleControllerImpl is
// associated with each SessionHost instance.
class ModuleControllerImpl : public ModuleController {
 public:
  ModuleControllerImpl(
      SessionHost* session,
      fidl::InterfacePtr<Module> module,
      fidl::InterfaceRequest<ModuleController> module_controller);
  ~ModuleControllerImpl();

  // Implements ModuleController.
  void Watch(fidl::InterfaceHandle<ModuleWatcher> watcher) override;

  // Called by SessionHost. Closes the module handle and notifies
  // watchers.
  void Done();

 private:
  SessionHost* const session_;
  StrongBinding<ModuleController> binding_;
  fidl::InterfacePtr<Module> module_;
  std::vector<fidl::InterfacePtr<ModuleWatcher>> watchers_;
  FTL_DISALLOW_COPY_AND_ASSIGN(ModuleControllerImpl);
};

// SessionHost keeps a single connection from a client (i.e., a module
// instance in the same session) to a SessionImpl together with
// pointers to all links created and modules started through this
// connection. This allows to persist and recreate the session state
// correctly.
class SessionHost : public Session {
 public:
  // Primary session host created when SessionImpl is created from story
  // manager.
  SessionHost(SessionImpl* impl, fidl::InterfaceRequest<Session> session);

  // Non-primary session host created for the module started by StartModule().
  SessionHost(SessionImpl* impl,
              fidl::InterfaceRequest<Session> session,
              fidl::InterfacePtr<Module> module,
              fidl::InterfaceRequest<ModuleController> module_controller);
  ~SessionHost() override;

  // Implements Session interface. Forwards to SessionImpl.
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
  SessionImpl* const impl_;
  StrongBinding<Session> binding_;
  ModuleControllerImpl* module_controller_;
  const bool primary_;
  FTL_DISALLOW_COPY_AND_ASSIGN(SessionHost);
};

// The actual implementation of the Session service. Called from
// SessionHost above.
class SessionImpl {
 public:
  SessionImpl(std::shared_ptr<ApplicationContext> application_context,
              fidl::InterfaceHandle<Resolver> resolver,
              fidl::InterfaceHandle<SessionStorage> session_storage,
              fidl::InterfaceRequest<Session> session_request);
  ~SessionImpl();

  // These methods are called by SessionHost.
  void Add(SessionHost* client);
  void Remove(SessionHost* client);
  void CreateLink(const fidl::String& name, fidl::InterfaceRequest<Link> link);
  void StartModule(const fidl::String& query,
                   fidl::InterfaceHandle<Link> link,
                   fidl::InterfaceRequest<ModuleController> module_controller,
                   fidl::InterfaceRequest<mozart::ViewOwner> view_owner);

 private:
  std::shared_ptr<ApplicationContext> application_context_;
  fidl::InterfacePtr<Resolver> resolver_;
  std::shared_ptr<SessionPage> page_;
  std::vector<SessionHost*> clients_;
  FTL_DISALLOW_COPY_AND_ASSIGN(SessionImpl);
};

// Shared owner of the connection to the ledger page. Shared between
// the SessionImpl, and all LinkImpls, so the connection is around
// until all Links are closed when the session shuts down.
class SessionPage {
 public:
  SessionPage(fidl::InterfaceHandle<SessionStorage> session_storage);
  ~SessionPage();

  void Init(std::function<void()> done);

  // These methods are called by LinkImpl.
  void MaybeReadLink(const fidl::String& name, MojoDocMap* data);
  void WriteLink(const fidl::String& name, const MojoDocMap& data);

 private:
  fidl::InterfacePtr<SessionStorage> session_storage_;
  fidl::StructPtr<SessionData> data_;
  fidl::Array<uint8_t> id_;  // logging only

  FTL_DISALLOW_COPY_AND_ASSIGN(SessionPage);
};

}  // namespace modular

#endif  // APPS_MODULAR_STORY_RUNNER_SESSION_H__
