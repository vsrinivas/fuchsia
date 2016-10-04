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
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"
#include "mojo/public/cpp/system/macros.h"
#include "mojo/public/interfaces/application/shell.mojom.h"

namespace modular {

class SessionImpl;

// SessionHost keeps a single connection from a client (i.e., a module
// instance in the same session) to a SessionImpl together with
// pointers to all links created and modules started through this
// connection. This allows to persist and recreate the session state
// correctly.
class SessionHost : public Session {
 public:
  SessionHost(SessionImpl* impl, mojo::InterfaceRequest<Session> req,
              bool primary);
  ~SessionHost();

  // Implements Session interface. Forwards to SessionImpl, therefore
  // the methods are implemented below, after SessionImpl is defined.
  void CreateLink(mojo::InterfaceRequest<Link> link) override;
  void StartModule(const mojo::String& query, mojo::InterfaceHandle<Link> link,
                   const StartModuleCallback& callback) override;

 private:
  // TODO(mesch): Actually record link and module instances created
  // through this binding here.
  SessionImpl* const impl_;
  mojo::StrongBinding<Session> binding_;
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
  void StartModule(SessionHost* client, const mojo::String& query,
                   mojo::InterfaceHandle<Link> link,
                   const SessionHost::StartModuleCallback& callback);

 private:
  // Used to pass interface handles into callback lambdas.
  int new_link_id_() { return link_id_++; }
  int link_id_ = 0;
  std::map<int, mojo::InterfaceHandle<Link>> link_map_;

  mojo::Shell* const shell_;
  mojo::InterfacePtr<Resolver> resolver_;
  mojo::InterfacePtr<ledger::Page> session_page_;
  std::vector<SessionHost*> clients_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(SessionImpl);
};

}  // namespace modular

#endif  // MOJO_APPS_MODULAR_STORY_RUNNER_SESSION_H__
