// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/maxwell/context_service/context_service.mojom.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"

#include "apps/maxwell/context_service/graph.h"
#include "apps/maxwell/context_service/repo.h"

namespace {

using mojo::ApplicationImplBase;
using mojo::ConnectionContext;
using mojo::InterfaceHandle;
using mojo::InterfaceRequest;
using mojo::ServiceProviderImpl;
using mojo::StrongBindingSet;

using namespace intelligence;
using namespace intelligence::context_service;

template <typename Interface>
class ContextPublisherClient : public virtual Interface {
 public:
  ContextPublisherClient(ComponentNode* component, Repo* repo)
      : component_(component), repo_(repo) {}

  void Publish(const mojo::String& label,
               const mojo::String& schema,
               InterfaceHandle<ContextPublisherController> controller,
               InterfaceRequest<ContextPublisherLink> link) override {
    DataNode* output = component_->EmplaceDataNode(label, schema);
    repo_->Index(output);
    output->SetPublisher(controller.Pass(), link.Pass());
  }

 private:
  ComponentNode* component_;
  Repo* repo_;
};

template <typename Interface>
class ContextSubscriberClient : public virtual Interface {
 public:
  ContextSubscriberClient(Repo* repo) : repo_(repo) {}
  virtual ~ContextSubscriberClient() {}

  // TODO(rosswang): additional backpressure modes. For now, just do
  // on-backpressure-buffer, which is the default for Mojo. When we add
  // backpressure, we'll probably add a callback, or add a reactive pull API.
  // TODO(rosswang): open-ended subscribe; right now, we just choose the first
  // match and ignore all others.
  virtual void Subscribe(const mojo::String& label,
                         const mojo::String& schema,
                         InterfaceHandle<ContextSubscriberLink> link_handle) {
    ContextSubscriberLinkPtr link =
        ContextSubscriberLinkPtr::Create(link_handle.Pass());
    // TODO(rosswang): add a meta-query for whether any known publishers exist.
    repo_->Query(label, schema, link.Pass());
  }

 private:
  Repo* repo_;
};

typedef ContextPublisherClient<ContextAcquirerClient> ContextAcquirerClientImpl;

class ContextAgentClientImpl
    : public ContextSubscriberClient<ContextAgentClient>,
      public ContextPublisherClient<ContextAgentClient> {
 public:
  ContextAgentClientImpl(ComponentNode* component, Repo* repo)
      : ContextSubscriberClient(repo),
        ContextPublisherClient(component, repo) {}
};

typedef ContextSubscriberClient<SuggestionAgentClient>
    SuggestionAgentClientImpl;

class ContextServiceApp : public ApplicationImplBase {
 public:
  ContextServiceApp() {}

  bool OnAcceptConnection(ServiceProviderImpl* service_provider_impl) override {
    // TODO(rosswang): Lifecycle management for ComponentNodes (and graph
    // structure in general). Right now, they are immortal. In the future, we'll
    // probably want to give the client class ownership.
    service_provider_impl->AddService<ContextAcquirerClient>(
        [this](const ConnectionContext& connection_context,
               InterfaceRequest<ContextAcquirerClient> request) {
          caq_clients_.AddBinding(
              new ContextAcquirerClientImpl(
                  new ComponentNode(connection_context.remote_url), &repo_),
              request.Pass());
        });
    service_provider_impl->AddService<ContextAgentClient>(
        [this](const ConnectionContext& connection_context,
               InterfaceRequest<ContextAgentClient> request) {
          cag_clients_.AddBinding(
              new ContextAgentClientImpl(
                  new ComponentNode(connection_context.remote_url), &repo_),
              request.Pass());
        });
    service_provider_impl->AddService<SuggestionAgentClient>(
        [this](const ConnectionContext& connection_context,
               InterfaceRequest<SuggestionAgentClient> request) {
          sag_clients_.AddBinding(new SuggestionAgentClientImpl(&repo_),
                                  request.Pass());
        });
    return true;
  }

 private:
  Repo repo_;
  StrongBindingSet<ContextAcquirerClient> caq_clients_;
  StrongBindingSet<ContextAgentClient> cag_clients_;
  StrongBindingSet<SuggestionAgentClient> sag_clients_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(ContextServiceApp);
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  ContextServiceApp app;
  return mojo::RunApplication(request, &app);
}
