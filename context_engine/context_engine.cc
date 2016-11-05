// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/context_engine.fidl.h"

#include "apps/modular/lib/app/application_context.h"
#include "lib/mtl/tasks/message_loop.h"

#include "apps/maxwell/context_engine/graph.h"
#include "apps/maxwell/context_engine/repo.h"

namespace {

using fidl::InterfaceHandle;
using fidl::InterfaceRequest;

using namespace maxwell::context_engine;

template <typename Interface>
class ContextPublisherClient : public virtual Interface {
 public:
  ContextPublisherClient(ComponentNode* component, Repo* repo)
      : component_(component), repo_(repo) {}

  void Publish(const fidl::String& label,
               const fidl::String& schema,
               InterfaceHandle<ContextPublisherController> controller,
               InterfaceRequest<ContextPublisherLink> link) override {
    DataNode* output = component_->EmplaceDataNode(label, schema);
    repo_->Index(output);
    output->SetPublisher(std::move(controller), std::move(link));
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
  // on-backpressure-buffer, which is the default for Mx. When we add
  // backpressure, we'll probably add a callback, or add a reactive pull API.
  // TODO(rosswang): open-ended subscribe; right now, we just choose the first
  // match and ignore all others.
  virtual void Subscribe(const fidl::String& label,
                         const fidl::String& schema,
                         InterfaceHandle<ContextSubscriberLink> link_handle) {
    ContextSubscriberLinkPtr link =
        ContextSubscriberLinkPtr::Create(std::move(link_handle));
    // TODO(rosswang): add a meta-query for whether any known publishers exist.
    repo_->Query(label, schema, std::move(link));
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

class ContextEngineApp : public ContextEngine {
 public:
  ContextEngineApp()
      : app_ctx_(modular::ApplicationContext::CreateFromStartupInfo()) {
    app_ctx_->outgoing_services()->AddService<ContextEngine>(
        [this](InterfaceRequest<ContextEngine> request) {
          provider_bindings_.AddBinding(this, std::move(request));
        });
  }

  void RegisterContextAcquirer(
      const fidl::String& url,
      InterfaceRequest<ContextAcquirerClient> client) override {
    RegisterClient(&caq_clients_, std::make_unique<ContextAcquirerClientImpl>(
                                      new ComponentNode(url), &repo_),
                   std::move(client));
  }

  void RegisterContextAgent(
      const fidl::String& url,
      InterfaceRequest<ContextAgentClient> client) override {
    RegisterClient(&cag_clients_, std::make_unique<ContextAgentClientImpl>(
                                      new ComponentNode(url), &repo_),
                   std::move(client));
  }

  void RegisterSuggestionAgent(
      const fidl::String& url,
      InterfaceRequest<SuggestionAgentClient> client) override {
    RegisterClient(&sag_clients_,
                   std::make_unique<SuggestionAgentClientImpl>(&repo_),
                   std::move(client));
  }

 private:
  template <class Interface>
  using UptrBindingSet =
      fidl::BindingSet<Interface, std::unique_ptr<Interface>>;

  // Although Impl is a bit redundant, including it allows the compiler to
  // infer type args.
  template <class Interface, class Impl>
  void RegisterClient(UptrBindingSet<Interface>* bindings,
                      std::unique_ptr<Impl> impl,
                      InterfaceRequest<Interface> client) {
    bindings->AddBinding(std::move(impl), std::move(client));
  }

  std::unique_ptr<modular::ApplicationContext> app_ctx_;
  Repo repo_;
  fidl::BindingSet<ContextEngine> provider_bindings_;
  UptrBindingSet<ContextAcquirerClient> caq_clients_;
  UptrBindingSet<ContextAgentClient> cag_clients_;
  UptrBindingSet<SuggestionAgentClient> sag_clients_;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  ContextEngineApp app;
  loop.Run();
  return 0;
}
