// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the story runner mojo app and of all mojo
// services it provides directly or transitively from other services.

#include <mojo/system/main.h>
#include <stdio.h>
#include <map>
#include <memory>
#include <vector>

#include "apps/modular/story_runner/story_runner.mojom.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/system/macros.h"
#include "mojo/public/interfaces/application/shell.mojom.h"

namespace modular {

using mojo::ApplicationImplBase;
using mojo::Array;
using mojo::BindingSet;
using mojo::ConnectionContext;
using mojo::GetProxy;
using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::ServiceProviderImpl;
using mojo::Shell;
using mojo::String;
using mojo::StrongBinding;
using mojo::StructPtr;

// A Link is a mutable and observable value shared between modules.
// When a module requests to run more modules using
// Session::StartModule(), a Link instance is associated with each
// such request, i.e. a Link instance is shared between at least two
// modules. The same Link instance can be used in multiple
// StartModule() requests, so it can be shared between more than two
// modules. The Dup() method allows to obtain more handles of the same
// Link instance.
//
// If a watcher is registered through one handle, it only receives
// notifications for changes by requests through other handles. To
// make this possible, each connection is associated with a separate
// implementation instance, called a host.
//
// A host can be primary. If it's primary, then it deletes the
// LinkImpl instance that is shared between all connections when it's
// closed, analog to a strong Binding.

class LinkImpl;

// LinkHost keeps a single connection from a client to a LinkImpl
// together with pointers to all watchers registered through this
// connection. We need this as a separate class so that we can
// identify where an updated value comes from, so that we are able to
// suppress notifications sent to the same client.
class LinkHost : public Link {
 public:
  LinkHost(LinkImpl* impl, InterfaceRequest<Link> req, bool primary);
  ~LinkHost();

  // Implements Link interface. Forwards to LinkImpl, therefore the
  // methods are implemented below, after LinkImpl is defined.
  void SetValue(StructPtr<LinkValue> value) override;
  void Value(const ValueCallback& callback) override;
  void Watch(InterfaceHandle<LinkChanged> watcher) override;
  void Dup(InterfaceRequest<Link> dup) override;

  // Called back from LinkImpl.
  void Notify(const StructPtr<LinkValue>& value);

 private:
  LinkImpl* const impl_;
  StrongBinding<Link> binding_;
  const bool primary_;
  std::vector<InterfacePtr<LinkChanged>> watchers_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(LinkHost);
};

// The actual implementation of the Link service. Called from LinkHost
// instances above.
class LinkImpl {
 public:
  explicit LinkImpl(InterfaceRequest<Link> req) {
    FTL_LOG(INFO) << "LinkImpl()";
    new LinkHost(this, std::move(req), true);  // Calls Add().
  }

  ~LinkImpl() {
    while (!clients_.empty()) {
      delete clients_.back();  // Calls Remove(), which erases the
                               // deleted element.
    }
  }

  // The methods below are all called by LinkHost.

  void Add(LinkHost* const client) { clients_.push_back(client); }

  void Remove(LinkHost* const client) {
    auto f = std::find(clients_.begin(), clients_.end(), client);
    FTL_DCHECK(f != clients_.end());
    clients_.erase(f);
  }

  // SetValue knows which client a notification comes from, so it
  // notifies only all other clients.
  void SetValue(LinkHost* const src, StructPtr<LinkValue> value) {
    value_ = std::move(value);
    for (auto dst : clients_) {
      if (dst != src) {
        dst->Notify(value_);
      }
    }
  }

  const StructPtr<LinkValue>& Value() const { return value_; }

 private:
  StructPtr<LinkValue> value_;
  std::vector<LinkHost*> clients_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(LinkImpl);
};


// The methods of LinkHost may call LinkImpl and thus need to be
// implemented after LinkImpl is defined.

LinkHost::LinkHost(LinkImpl* const impl, InterfaceRequest<Link> req,
                   const bool primary)
    : impl_(impl), binding_(this, std::move(req)), primary_(primary) {
  FTL_LOG(INFO) << "LinkHost()" << (primary_ ? " primary" : "");
  impl_->Add(this);
}

LinkHost::~LinkHost() {
  FTL_LOG(INFO) << "~LinkHost()";
  impl_->Remove(this);

  // If a "primary" (currently that's the first) connection goes down,
  // the whole implementation is deleted, taking down all remaining
  // connections. This corresponds to a strong binding on the first
  // connection, and regular bindings on all later ones. This is just
  // how it is and may be revised in the future.
  //
  // Order is important: this delete call MUST happen after the
  // Remove() call above, otherwise double delete ensues.
  if (primary_) {
    delete impl_;
  }
}

void LinkHost::SetValue(StructPtr<LinkValue> value) {
  impl_->SetValue(this, std::move(value));
}

void LinkHost::Value(const ValueCallback& callback) {
  callback.Run(impl_->Value().Clone());
}

void LinkHost::Watch(InterfaceHandle<LinkChanged> watcher) {
  InterfacePtr<LinkChanged> watcher_ptr;
  watcher_ptr.Bind(watcher.Pass());

  // The current Value is sent to a newly registered watcher only if
  // it's not null.
  if (!impl_->Value().is_null()) {
    watcher_ptr->Value(impl_->Value().Clone());
  }

  watchers_.push_back(std::move(watcher_ptr));
}

void LinkHost::Dup(InterfaceRequest<Link> dup) {
  new LinkHost(impl_, std::move(dup), false);
}

void LinkHost::Notify(const StructPtr<LinkValue>& value) {
  for (InterfacePtr<LinkChanged>& watcher : watchers_) {
    watcher->Value(value.Clone());
  }
}

// The Session is the context in which a story executes. It starts
// modules and provides them with a handle to itself, so they can
// start more modules. It also serves as the factory for Link
// instances, which are used to share data between modules.

class SessionImpl;

// SessionHost keeps a single connection from a client (i.e., a module
// instance in the same session) to a SessionImpl together with
// pointers to all links created and modules started through this
// connection. This allows to persist and recreate the session state
// correctly.
class SessionHost : public Session {
 public:
  SessionHost(SessionImpl* const impl, InterfaceRequest<Session> req,
              const bool primary);
  ~SessionHost();

  // Implements Session interface. Forwards to SessionImpl, therefore
  // the methods are implemented below, after SessionImpl is defined.
  void CreateLink(InterfaceRequest<Link> link) override;
  void StartModule(const String& query, InterfaceHandle<Link> link,
                   const StartModuleCallback& callback) override;

 private:
  // TODO(mesch): Actually record link and module instances created
  // through this binding here.
  SessionImpl* const impl_;
  StrongBinding<Session> binding_;
  const bool primary_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(SessionHost);
};

// The actual implementation of the Session service. Called from
// SessionHost above.
class SessionImpl {
 public:
  SessionImpl(Shell* const shell, InterfaceHandle<Resolver> resolver,
              InterfaceHandle<ledger::Page> session_page,
              InterfaceRequest<Session> req)
      : shell_(shell) {
    FTL_LOG(INFO) << "SessionImpl()";
    resolver_.Bind(resolver.Pass());
    session_page_.Bind(session_page.Pass());
    session_page_->GetId([](Array<uint8_t> id) {
      std::string string_id;
      for (uint8_t val : id) {
        string_id += std::to_string(val);
      }
      FTL_LOG(INFO) << "story-runner init session with session page: "
                    << string_id;
    });

    new SessionHost(this, std::move(req), true);  // Calls Add();
  }

  ~SessionImpl() {
    FTL_LOG(INFO) << "~SessionImpl()";
    while (!clients_.empty()) {
      delete clients_.back();  // Calls Remove(), which erases the
                               // deleted element.
    }
  }

  // These methods are called by SessionHost.

  void Add(SessionHost* const client) {
    clients_.push_back(client);
  }

  void Remove(SessionHost* const client) {
    auto f = std::find(clients_.begin(), clients_.end(), client);
    FTL_DCHECK(f != clients_.end());
    clients_.erase(f);
  }

  void StartModule(SessionHost* const client,
                   const String& query, InterfaceHandle<Link> link,
                   const SessionHost::StartModuleCallback& callback) {
    FTL_LOG(INFO) << "story-runner start module " << query;

    const int link_id = new_link_id_();
    link_map_[link_id] = link.Pass();

    resolver_->Resolve(
        query, [client, this, link_id, callback](String module_url) {
          // TODO(mesch): Client is not yet used. We need to remember
          // the association of which module was requested from which
          // other module, and what link instance was exchanged
          // between them. We will do this by associating the link
          // instances with names which are local to the module that
          // uses them.

          InterfacePtr<Module> module;
          mojo::ConnectToService(shell_, module_url, GetProxy(&module));

          InterfaceHandle<Session> self;
          new SessionHost(this, GetProxy(&self), false);

          module->Initialize(std::move(self), link_map_[link_id].Pass());
          link_map_.erase(link_id);

          callback.Run(module.PassInterfaceHandle());
        });
  }

 private:
  // Used to pass interface handles into callback lambdas.
  int new_link_id_() { return link_id_++; }
  int link_id_ = 0;
  std::map<int, InterfaceHandle<Link>> link_map_;

  Shell* const shell_;
  InterfacePtr<Resolver> resolver_;
  InterfacePtr<ledger::Page> session_page_;

  std::vector<SessionHost*> clients_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(SessionImpl);
};


// The methods of SessionHost may call SessionImpl and thus need to be
// implemented after SessionImpl is defined.

SessionHost::SessionHost(SessionImpl* const impl, InterfaceRequest<Session> req,
                         const bool primary)
    : impl_(impl), binding_(this, std::move(req)), primary_(primary) {
  impl_->Add(this);
}

SessionHost::~SessionHost() {
  impl_->Remove(this);

  // If a "primary" (currently that's the first) connection goes down,
  // the whole implementation is deleted, taking down all remaining
  // connections. This corresponds to a strong binding on the first
  // connection, and regular bindings on all later ones. This is just
  // how it is and may be revised in the future.
  //
  // Order is important: this delete call MUST happen after the
  // Remove() call above, otherwise double delete ensues.
  if (primary_) {
    delete impl_;
  }
}

void SessionHost::CreateLink(InterfaceRequest<Link> link) {
  FTL_LOG(INFO) << "story-runner create link";
  new LinkImpl(std::move(link));
}

void SessionHost::StartModule(const String& query, InterfaceHandle<Link> link,
                              const StartModuleCallback& callback) {
  impl_->StartModule(this, query, std::move(link), callback);
}


// The story runner service is the service directly provided by the
// story runner app. It must be initialized with a resolver factory
// and then allows to create a Session.
class StoryRunnerImpl : public StoryRunner {
 public:
  StoryRunnerImpl(Shell* const shell, InterfaceRequest<StoryRunner> req)
      : shell_(shell), binding_(this, std::move(req)) {
    FTL_LOG(INFO) << "StoryRunnerImpl()";
  }

  ~StoryRunnerImpl() override { FTL_LOG(INFO) << "~StoryRunnerImpl()"; }

  void Initialize(InterfaceHandle<ResolverFactory> resolver_factory) override {
    resolver_factory_.Bind(resolver_factory.Pass());
  }

  void StartStory(InterfaceHandle<ledger::Page> session_page,
                  InterfaceRequest<Session> session) override {
    InterfaceHandle<Resolver> resolver;
    resolver_factory_->GetResolver(GetProxy(&resolver));
    new SessionImpl(shell_, resolver.Pass(), session_page.Pass(),
                    std::move(session));
  }

 private:
  Shell* const shell_;
  InterfacePtr<ResolverFactory> resolver_factory_;
  StrongBinding<StoryRunner> binding_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(StoryRunnerImpl);
};


// The StoryRunner mojo app provides instances of the implementation
// of the StoryRunner service. It is a single service app, but the
// service impl takes the shell as additional constructor parameter,
// so we don't reuse the template class here.
class StoryRunnerApp : public ApplicationImplBase {
 public:
  StoryRunnerApp() {}
  ~StoryRunnerApp() override {}

  bool OnAcceptConnection(ServiceProviderImpl* const s) override {
    s->AddService<StoryRunner>([this](const ConnectionContext& ctx,
                                      InterfaceRequest<StoryRunner> req) {
      new StoryRunnerImpl(shell(), std::move(req));
    });

    return true;
  }

 private:
  MOJO_DISALLOW_COPY_AND_ASSIGN(StoryRunnerApp);
};

}  // namespace modular

MojoResult MojoMain(MojoHandle request) {
  FTL_LOG(INFO) << "story-runner main";
  modular::StoryRunnerApp app;
  return mojo::RunApplication(request, &app);
}
