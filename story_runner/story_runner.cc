// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the story runner mojo app and of all mojo
// services it provides directly or transitively from other services.

#include <mojo/system/main.h>
#include <stdio.h>
#include <map>
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
#include "mojo/public/cpp/environment/logging.h"
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
class LinkImpl : public Link {
 public:
  explicit LinkImpl(InterfaceRequest<Link> req)
      : binding_(this, std::move(req)) {}

  ~LinkImpl() override {}

  void SetValue(const String& label, const String& value) override {
    FTL_LOG(INFO) << "story-runner link set value " << label << ": " << value;

    values_[label] = value;

    for (auto& watcher : watchers_) {
      watcher->Value(label, value);
    }

    FTL_LOG(INFO) << "story-runner link set value return";
  }

  void Value(const String& label, const ValueCallback& callback) override {
    callback.Run(values_[label]);
  }

  void Watch(InterfaceHandle<LinkChanged> watcher) override {
    FTL_LOG(INFO) << "story-runner link watch";

    InterfacePtr<LinkChanged> watcher_ptr =
        InterfacePtr<LinkChanged>::Create(watcher.Pass());

    for (auto& value : values_) {
      watcher_ptr->Value(value.first, value.second);
    }

    watchers_.push_back(std::move(watcher_ptr));

    FTL_LOG(INFO) << "story-runner link watch return";
  }

  void Dup(InterfaceRequest<Link> dup) override {
    FTL_LOG(INFO) << "story-runner link dup";

    clones_.AddBinding(this, std::move(dup));

    FTL_LOG(INFO) << "story-runner link dup return";
  }

 private:
  StrongBinding<Link> binding_;
  BindingSet<Link> clones_;
  std::vector<InterfacePtr<LinkChanged>> watchers_;
  std::map<String, String> values_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(LinkImpl);
};

// The Session is the context in which a story executes.
class SessionImpl : public Session {
 public:
  SessionImpl(Shell* shell, InterfaceHandle<Resolver> resolver,
              InterfaceHandle<ledger::Page> session_page,
              InterfaceRequest<Session> req)
      : shell_(shell), binding_(this, std::move(req)) {
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

  ~SessionImpl() override {}

  void CreateLink(const String& schema, InterfaceRequest<Link> link) override {
    FTL_LOG(INFO) << "story-runner create link";

    new LinkImpl(std::move(link));

    FTL_LOG(INFO) << "story-runner create link return";
  }

  void StartModule(const String& query, InterfaceHandle<Link> link,
                   const StartModuleCallback& callback) override {
    FTL_LOG(INFO) << "story-runner start module";
    std::string new_link_id = GenerateId();
    resolver_->Resolve(query, [this, new_link_id, callback](String module_url) {
      InterfacePtr<Module> module;
      mojo::ConnectToService(shell_, module_url, GetProxy(&module));

      InterfaceHandle<Session> self;
      bindings_.AddBinding(this, GetProxy(&self));

      module->Initialize(std::move(self), link_map_[new_link_id].Pass());
      link_map_.erase(new_link_id);

      callback.Run(module.PassInterfaceHandle());

      FTL_LOG(INFO) << "story-runner start module return";
    });

    link_map_.emplace(std::move(new_link_id), link.Pass());
  }

 private:
  std::string GenerateId() {
    std::string id = std::to_string(counter);
    counter++;
    return id;
  }

  Shell* const shell_;
  std::map<std::string, InterfaceHandle<Link>> link_map_;
  InterfacePtr<Resolver> resolver_;
  InterfacePtr<ledger::Page> session_page_;
  StrongBinding<Session> binding_;
  BindingSet<Session> bindings_;

  // Used to generate IDs for the link handles.
  int counter = 0;

  MOJO_DISALLOW_COPY_AND_ASSIGN(SessionImpl);
};

// The story runner service is the primary service provided by the
// story runner app. It allows to create a Session.
class StoryRunnerImpl : public StoryRunner {
 public:
  StoryRunnerImpl(Shell* const shell, InterfaceRequest<StoryRunner> req)
      : shell_(shell), binding_(this, std::move(req)) {}
  ~StoryRunnerImpl() override {}

  void Initialize(InterfaceHandle<ResolverFactory> resolver_factory) override {
    resolver_factory_.Bind(resolver_factory.Pass());
  }

  void StartStory(InterfaceHandle<ledger::Page> session_page,
                  InterfaceRequest<Session> session) override {
    FTL_LOG(INFO) << "story-runner start story";

    InterfaceHandle<Resolver> resolver;
    resolver_factory_->GetResolver(GetProxy(&resolver));
    new SessionImpl(shell_, resolver.Pass(), session_page.Pass(),
                    std::move(session));

    FTL_LOG(INFO) << "story-runner start story return";
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
    FTL_LOG(INFO) << "story-runner accept connection";

    s->AddService<StoryRunner>([this](const ConnectionContext& ctx,
                                      InterfaceRequest<StoryRunner> req) {
      FTL_LOG(INFO) << "story-runner service request";

      new StoryRunnerImpl(shell(), std::move(req));

      FTL_LOG(INFO) << "story-runner service request return";
    });

    FTL_LOG(INFO) << "story-runner accept connection return";

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
