// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/context/context_engine.fidl.h"

#include "apps/modular/lib/app/application_context.h"
#include "lib/mtl/tasks/message_loop.h"

#include "apps/maxwell/src/context_engine/graph.h"
#include "apps/maxwell/src/context_engine/repo.h"

namespace {

using fidl::InterfaceHandle;
using fidl::InterfaceRequest;

using namespace maxwell::context;

template <typename Interface>
class PublisherClient : public virtual Interface {
 public:
  PublisherClient(ComponentNode* component, Repo* repo)
      : component_(component), repo_(repo) {}

  void Publish(const fidl::String& label,
               const fidl::String& schema,
               InterfaceHandle<PublisherController> controller,
               InterfaceRequest<PublisherLink> link) override {
    DataNode* output = component_->EmplaceDataNode(label, schema);
    repo_->Index(output);
    output->SetPublisher(std::move(controller), std::move(link));
  }

 private:
  ComponentNode* component_;
  Repo* repo_;
};

template <typename Interface>
class SubscriberClient : public virtual Interface {
 public:
  SubscriberClient(Repo* repo) : repo_(repo) {}
  virtual ~SubscriberClient() {}

  // TODO(rosswang): additional backpressure modes. For now, just do
  // on-backpressure-buffer, which is the default for Mx. When we add
  // backpressure, we'll probably add a callback, or add a reactive pull API.
  // TODO(rosswang): open-ended subscribe; right now, we just choose the first
  // match and ignore all others.
  virtual void Subscribe(const fidl::String& label,
                         const fidl::String& schema,
                         InterfaceHandle<SubscriberLink> link_handle) {
    SubscriberLinkPtr link = SubscriberLinkPtr::Create(std::move(link_handle));
    // TODO(rosswang): add a meta-query for whether any known publishers exist.
    repo_->Query(label, schema, std::move(link));
  }

 private:
  Repo* repo_;
};

typedef PublisherClient<ContextAcquirerClient> ContextAcquirerClientImpl;

class ContextAgentClientImpl : public SubscriberClient<ContextAgentClient>,
                               public PublisherClient<ContextAgentClient> {
 public:
  ContextAgentClientImpl(ComponentNode* component, Repo* repo)
      : SubscriberClient(repo), PublisherClient(component, repo) {}
};

typedef SubscriberClient<SuggestionAgentClient> SuggestionAgentClientImpl;

class ContextEngineApp : public ContextEngine {
 public:
  ContextEngineApp()
      : app_context_(modular::ApplicationContext::CreateFromStartupInfo()) {
    app_context_->outgoing_services()->AddService<ContextEngine>(
        [this](InterfaceRequest<ContextEngine> request) {
          bindings_.AddBinding(this, std::move(request));
        });
  }

  void RegisterContextAcquirer(
      const fidl::String& url,
      InterfaceRequest<ContextAcquirerClient> client) override {
    caq_bindings_.AddBinding(std::make_unique<ContextAcquirerClientImpl>(
                                 new ComponentNode(url), &repo_),
                             std::move(client));
  }

  void RegisterContextAgent(
      const fidl::String& url,
      InterfaceRequest<ContextAgentClient> client) override {
    cag_bindings_.AddBinding(std::make_unique<ContextAgentClientImpl>(
                                 new ComponentNode(url), &repo_),
                             std::move(client));
  }

  void RegisterSuggestionAgent(
      const fidl::String& url,
      InterfaceRequest<SuggestionAgentClient> client) override {
    sag_bindings_.AddBinding(
        std::make_unique<SuggestionAgentClientImpl>(&repo_), std::move(client));
  }

 private:
  template <class Interface>
  using UptrBindingSet =
      fidl::BindingSet<Interface, std::unique_ptr<Interface>>;

  std::unique_ptr<modular::ApplicationContext> app_context_;

  Repo repo_;

  fidl::BindingSet<ContextEngine> bindings_;
  UptrBindingSet<ContextAcquirerClient> caq_bindings_;
  UptrBindingSet<ContextAgentClient> cag_bindings_;
  UptrBindingSet<SuggestionAgentClient> sag_bindings_;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  ContextEngineApp app;
  loop.Run();
  return 0;
}
