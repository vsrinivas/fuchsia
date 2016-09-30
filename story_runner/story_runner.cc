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

// A Link is a mutable and observable value shared between modules.
// When a module requests to run more modules using
// Session::StartModule(), a Link instance is associated with each
// such request, i.e. a Link instance is shared between at least two
// modules. The same Link instance can be used in multiple
// StartModule() requests, so it can be shared between more than two
// modules. The Dup() method allows to obtain more handles of the same
// Link instance.
//
// If a watcher is registered through one handle, it can be configured
// that it only receives notifications for changes caused by requested
// through other handles. To make this possible, each connection is
// associated with a separate implementation instance, called a host.
//
// A host can be primary. If it's primary, then it deletes the
// LinkImpl instance that is shared between all connections when it's
// closed, analog to a strong Binding.

class LinkImpl;

// LinkHost keeps a single connection to a LinkImpl together with
// pointers to all watchers registered through this connection. We
// need this as a separate class so that we can identify where an
// updated value comes from, so that we are able to suppress
// notifications sent to the same client.
class LinkHost : public Link {
 public:
  LinkHost(LinkImpl* impl, InterfaceRequest<Link> req, bool primary);
  ~LinkHost();

  // Implements Link interface. Forwards to LinkImpl, therefore the
  // methods are implemented below, after LinkImpl is defined.
  void SetValue(const String& label, const String& value) override;
  void Value(const String& label, const ValueCallback& callback) override;
  void Watch(InterfaceHandle<LinkChanged> watcher, bool self) override;
  void Dup(InterfaceRequest<Link> dup) override;

  // Called back from LinkImpl.
  void Notify(LinkHost* source, const String& label, const String& value);

 private:
  LinkImpl* const impl_;
  StrongBinding<Link> binding_;
  const bool primary_;
  std::vector<std::pair<InterfacePtr<LinkChanged>, bool>> watchers_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(LinkHost);
};

// The actual implementation of the Link service. Called from LinkHost
// instances above.
class LinkImpl {
 public:
  explicit LinkImpl(InterfaceRequest<Link> req) {
    FTL_LOG(INFO) << "LinkImpl()";
    new LinkHost(this, std::move(req), true);
  }

  ~LinkImpl() {
    FTL_LOG(INFO) << "~LinkImpl()";
    std::vector<LinkHost*> clients;
    clients.swap(clients_);
    for (auto client : clients) {
      delete client;
    }
  }

  // The methods below are all called by LinkHost.

  void Add(LinkHost* const client) { clients_.push_back(client); }

  void Remove(LinkHost* const client) {
    auto f = std::find(clients_.begin(), clients_.end(), client);
    if (f != clients_.end()) {
      clients_.erase(f);
    }
  }

  void SetValue(LinkHost* const impl, const String& label,
                const String& value) {
    values_[label] = value;
    for (auto& client : clients_) {
      client->Notify(impl, label, value);
    }
  }

  const String& Value(const String& label) { return values_[label]; }

  const std::map<String, String>& values() const { return values_; }

 private:
  std::map<String, String> values_;
  std::vector<LinkHost*> clients_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(LinkImpl);
};

// Methods of LinkHost that need the definition of LinkImpl.
LinkHost::LinkHost(LinkImpl* const impl, InterfaceRequest<Link> req,
                   const bool primary)
    : impl_(impl), binding_(this, std::move(req)), primary_(primary) {
  FTL_LOG(INFO) << "LinkHost()" << (primary_ ? " primary" : "");
  impl_->Add(this);
}

LinkHost::~LinkHost() {
  FTL_LOG(INFO) << "~LinkHost()";
  impl_->Remove(this);
  if (primary_) {
    delete impl_;
  }
}

void LinkHost::SetValue(const String& label, const String& value) {
  impl_->SetValue(this, label, value);
}

void LinkHost::Value(const String& label, const ValueCallback& callback) {
  callback.Run(impl_->Value(label));
}

void LinkHost::Watch(InterfaceHandle<LinkChanged> watcher, const bool self) {
  InterfacePtr<LinkChanged> watcher_ptr;
  watcher_ptr.Bind(watcher.Pass());

  for (auto& value : impl_->values()) {
    watcher_ptr->Value(value.first, value.second);
  }

  watchers_.push_back(std::make_pair(std::move(watcher_ptr), self));
}

void LinkHost::Dup(InterfaceRequest<Link> dup) {
  new LinkHost(impl_, std::move(dup), false);
}

void LinkHost::Notify(LinkHost* const source, const String& label,
                      const String& value) {
  for (std::pair<InterfacePtr<LinkChanged>, bool>& watcher : watchers_) {
    if (watcher.second || source != this) {
      watcher.first->Value(label, value);
    }
  }
}

// The Session is the context in which a story executes.
class SessionImpl : public Session {
 public:
  SessionImpl(Shell* const shell, InterfaceHandle<Resolver> resolver,
              InterfaceHandle<ledger::Page> session_page,
              InterfaceRequest<Session> req)
      : shell_(shell), binding_(this, std::move(req)) {
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
  }

  ~SessionImpl() override { FTL_LOG(INFO) << "~SessionImpl()"; }

  void CreateLink(const String& schema, InterfaceRequest<Link> link) override {
    FTL_LOG(INFO) << "story-runner create link";
    new LinkImpl(std::move(link));
  }

  void StartModule(const String& query, InterfaceHandle<Link> link,
                   const StartModuleCallback& callback) override {
    FTL_LOG(INFO) << "story-runner start module " << query;

    const int link_id = new_link_id_();
    link_map_[link_id] = link.Pass();

    resolver_->Resolve(
        query, [this, link_id, callback, query](String module_url) {
          InterfacePtr<Module> module;
          mojo::ConnectToService(shell_, module_url, GetProxy(&module));

          InterfaceHandle<Session> self;
          bindings_.AddBinding(this, GetProxy(&self));

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
  StrongBinding<Session> binding_;
  BindingSet<Session> bindings_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(SessionImpl);
};

// The story runner service is the primary service provided by the
// story runner app. It allows to create a Session.
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

// The story runner mojo app.
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
