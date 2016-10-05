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
#include <vector>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/modular/story_runner/resolver.mojom.h"
#include "apps/modular/story_runner/session.mojom.h"
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

// Implements the ModuleClient interface, which is passed back to the
// client that requested a module to be started this SessionHost is
// passed to on Initialize(). One instance of ModuleClientImpl is
// associated with each SessionHost instance.
class ModuleClientImpl : public ModuleClient {
 public:
  ModuleClientImpl(SessionHost* session,
                   mojo::InterfacePtr<Module> module,
                   mojo::InterfaceRequest<ModuleClient> module_client);
  ~ModuleClientImpl();

  // Implements ModuleClient.
  void Watch(mojo::InterfaceHandle<ModuleWatcher> watcher) override;

  // Called by SessionHost. Closes the module handle and notifies
  // watchers.
  void Done();

 private:
  SessionHost* const session_;
  mojo::StrongBinding<ModuleClient> binding_;
  mojo::InterfacePtr<Module> module_;
  std::vector<mojo::InterfacePtr<ModuleWatcher>> watchers_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(ModuleClientImpl);
};

// SessionHost keeps a single connection from a client (i.e., a module
// instance in the same session) to a SessionImpl together with
// pointers to all links created and modules started through this
// connection. This allows to persist and recreate the session state
// correctly.
class SessionHost : public Session {
 public:
  // Primary session host created when SessionImpl is created from story manager.
  SessionHost(SessionImpl* impl,
              mojo::InterfaceRequest<Session> session);

  // Non-primary session host created for the module started by StartModule().
  SessionHost(SessionImpl* impl,
              mojo::InterfaceRequest<Session> session,
              mojo::InterfacePtr<Module> module,
              mojo::InterfaceRequest<ModuleClient> module_client);
  ~SessionHost();

  // Implements Session interface. Forwards to SessionImpl.
  void CreateLink(mojo::InterfaceRequest<Link> link) override;
  void StartModule(const mojo::String& query, mojo::InterfaceHandle<Link> link,
                   mojo::InterfaceRequest<ModuleClient> module_client) override;
  void Done() override;

  // Called by ModuleClientImpl.
  void Add(ModuleClientImpl* module_client);
  void Remove(ModuleClientImpl* module_client);

 private:
  // TODO(mesch): Actually record link instances created through this
  // binding here.
  SessionImpl* const impl_;
  mojo::StrongBinding<Session> binding_;
  ModuleClientImpl* module_client_;
  const bool primary_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(SessionHost);
};

// The actual implementation of the Session service. Called from
// SessionHost above.
class SessionImpl {
 public:
  SessionImpl(mojo::Shell* shell, mojo::InterfaceHandle<Resolver> resolver,
              mojo::InterfaceHandle<ledger::Page> session_page,
              mojo::InterfaceRequest<Session> req);
  ~SessionImpl();

  // These methods are called by SessionHost.
  void Add(SessionHost* client);
  void Remove(SessionHost* client);
  void StartModule(const mojo::String& query,
                   mojo::InterfaceHandle<Link> link,
                   mojo::InterfaceRequest<ModuleClient> module_client);

 private:
  // Used to pass handles into callback lambdas.
  int new_request_id_() { return request_id_++; }
  int request_id_ = 0;
  std::map<int, mojo::InterfaceHandle<Link>> link_map_;
  std::map<int, mojo::InterfaceRequest<ModuleClient>> module_client_map_;

  mojo::Shell* const shell_;
  mojo::InterfacePtr<Resolver> resolver_;
  mojo::InterfacePtr<ledger::Page> session_page_;
  std::vector<SessionHost*> clients_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(SessionImpl);
};


}  // namespace modular

#endif  // MOJO_APPS_MODULAR_STORY_RUNNER_SESSION_H__
