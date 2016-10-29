// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The Session is the context in which a story executes. It starts
// modules and provides them with a handle to itself, so they can
// start more modules. It also serves as the factory for Link
// instances, which are used to share data between modules.

#ifndef MOJO_APPS_MODULAR_STORY_RUNNER_SESSION_H__
#define MOJO_APPS_MODULAR_STORY_RUNNER_SESSION_H__

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "apps/document_store/interfaces/document.mojom.h"
#include "apps/modular/document_editor/document_editor.h"
#include "apps/ledger/api/ledger.mojom.h"
#include "apps/modular/services/story/resolver.mojom.h"
#include "apps/modular/services/story/session.mojom.h"
#include "apps/mozart/services/views/interfaces/view_token.mojom.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"
#include "mojo/public/cpp/system/macros.h"
#include "mojo/public/interfaces/application/shell.mojom.h"

namespace modular {

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
      mojo::InterfacePtr<Module> module,
      mojo::InterfaceRequest<ModuleController> module_controller);
  ~ModuleControllerImpl();

  // Implements ModuleController.
  void Watch(mojo::InterfaceHandle<ModuleWatcher> watcher) override;

  // Called by SessionHost. Closes the module handle and notifies
  // watchers.
  void Done();

 private:
  SessionHost* const session_;
  mojo::StrongBinding<ModuleController> binding_;
  mojo::InterfacePtr<Module> module_;
  std::vector<mojo::InterfacePtr<ModuleWatcher>> watchers_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(ModuleControllerImpl);
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
  SessionHost(SessionImpl* impl, mojo::InterfaceRequest<Session> session);

  // Non-primary session host created for the module started by StartModule().
  SessionHost(SessionImpl* impl,
              mojo::InterfaceRequest<Session> session,
              mojo::InterfacePtr<Module> module,
              mojo::InterfaceRequest<ModuleController> module_controller);
  ~SessionHost() override;

  // Implements Session interface. Forwards to SessionImpl.
  void CreateLink(const mojo::String& name,
                  mojo::InterfaceRequest<Link> link) override;
  void StartModule(
      const mojo::String& query,
      mojo::InterfaceHandle<Link> link,
      mojo::InterfaceRequest<ModuleController> module_controller,
      mojo::InterfaceRequest<mozart::ViewOwner> view_owner) override;
  void Done() override;

  // Called by ModuleControllerImpl.
  void Add(ModuleControllerImpl* module_controller);
  void Remove(ModuleControllerImpl* module_controller);

 private:
  SessionImpl* const impl_;
  mojo::StrongBinding<Session> binding_;
  ModuleControllerImpl* module_controller_;
  const bool primary_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(SessionHost);
};

// The actual implementation of the Session service. Called from
// SessionHost above.
class SessionImpl {
 public:
  SessionImpl(mojo::Shell* shell,
              mojo::InterfaceHandle<Resolver> resolver,
              mojo::InterfaceHandle<ledger::Page> session_page,
              mojo::InterfaceRequest<Session> req);
  ~SessionImpl();

  // These methods are called by SessionHost.
  void Add(SessionHost* client);
  void Remove(SessionHost* client);
  void CreateLink(const mojo::String& name, mojo::InterfaceRequest<Link> link);
  void StartModule(const mojo::String& query,
                   mojo::InterfaceHandle<Link> link,
                   mojo::InterfaceRequest<ModuleController> module_controller,
                   mojo::InterfaceRequest<mozart::ViewOwner> view_owner);

 private:
  mojo::Shell* const shell_;
  mojo::InterfacePtr<Resolver> resolver_;
  std::shared_ptr<SessionPage> page_;
  std::vector<SessionHost*> clients_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(SessionImpl);
};

// Shared owner of the connection to the ledger page. Shared between
// the SessionImpl, and all LinkImpls, so the connection is around
// until all Links are closed when the session shuts down.
class SessionPage {
 public:
  SessionPage(mojo::InterfaceHandle<ledger::Page> session_page);
  ~SessionPage();

  void Init(std::function<void()> done);

  // These methods are called by LinkImpl.
  void MaybeReadLink(const mojo::String& name, MojoDocMap* data) const;
  void WriteLink(const mojo::String& name, const MojoDocMap& data);

 private:
  mojo::InterfacePtr<ledger::Page> session_page_;
  mojo::InterfacePtr<ledger::PageSnapshot> session_page_snapshot_;
  mojo::StructPtr<SessionData> data_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(SessionPage);
};

}  // namespace modular

#endif  // MOJO_APPS_MODULAR_STORY_RUNNER_SESSION_H__
