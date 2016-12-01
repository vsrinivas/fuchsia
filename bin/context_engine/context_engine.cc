// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/src/context_engine/graph.h"
#include "apps/maxwell/src/context_engine/repo.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/mtl/tasks/message_loop.h"

namespace maxwell {
namespace {

template <typename Interface>
class PublisherClient : public virtual Interface {
 public:
  PublisherClient(ComponentNode* component, Repo* repo)
      : component_(component), repo_(repo) {}

  void Publish(const fidl::String& label,
               const fidl::String& schema,
               fidl::InterfaceHandle<ContextPublisherController> controller,
               fidl::InterfaceRequest<ContextPublisherLink> link) override {
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
  virtual void Subscribe(
      const fidl::String& label,
      const fidl::String& schema,
      fidl::InterfaceHandle<ContextSubscriberLink> link_handle) {
    ContextSubscriberLinkPtr link =
        ContextSubscriberLinkPtr::Create(std::move(link_handle));
    // TODO(rosswang): add a meta-query for whether any known publishers exist.
    repo_->Query(label, schema, std::move(link));
  }

 private:
  Repo* repo_;
};

typedef PublisherClient<ContextPublisher> ContextPublisherImpl;

class ContextPubSubImpl : public SubscriberClient<ContextPubSub>,
                          public PublisherClient<ContextPubSub> {
 public:
  ContextPubSubImpl(ComponentNode* component, Repo* repo)
      : SubscriberClient(repo), PublisherClient(component, repo) {}
};

typedef SubscriberClient<ContextSubscriber> ContextSubscriberImpl;

class ContextEngineApp : public ContextEngine {
 public:
  ContextEngineApp()
      : app_context_(modular::ApplicationContext::CreateFromStartupInfo()) {
    app_context_->outgoing_services()->AddService<ContextEngine>(
        [this](fidl::InterfaceRequest<ContextEngine> request) {
          bindings_.AddBinding(this, std::move(request));
        });
  }

  void RegisterContextAcquirer(
      const fidl::String& url,
      fidl::InterfaceRequest<ContextPublisher> client) override {
    caq_bindings_.AddBinding(
        std::make_unique<ContextPublisherImpl>(new ComponentNode(url), &repo_),
        std::move(client));
  }

  void RegisterContextAgent(
      const fidl::String& url,
      fidl::InterfaceRequest<ContextPubSub> client) override {
    cag_bindings_.AddBinding(
        std::make_unique<ContextPubSubImpl>(new ComponentNode(url), &repo_),
        std::move(client));
  }

  void RegisterSuggestionAgent(
      const fidl::String& url,
      fidl::InterfaceRequest<ContextSubscriber> client) override {
    sag_bindings_.AddBinding(std::make_unique<ContextSubscriberImpl>(&repo_),
                             std::move(client));
  }

 private:
  template <class Interface>
  using UptrBindingSet =
      fidl::BindingSet<Interface, std::unique_ptr<Interface>>;

  std::unique_ptr<modular::ApplicationContext> app_context_;

  Repo repo_;

  fidl::BindingSet<ContextEngine> bindings_;
  UptrBindingSet<ContextPublisher> caq_bindings_;
  UptrBindingSet<ContextPubSub> cag_bindings_;
  UptrBindingSet<ContextSubscriber> sag_bindings_;
};

}  // namespace
}  // namespace maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::ContextEngineApp app;
  loop.Run();
  return 0;
}
