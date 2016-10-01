// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/maxwell/context_service/context_service.mojom.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/interface_ptr_set.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"

namespace {

using std::shared_ptr;

using mojo::ApplicationImplBase;
using mojo::ConnectionContext;
using mojo::InterfaceHandle;
using mojo::InterfacePtrSet;
using mojo::InterfaceRequest;
using mojo::ServiceProviderImpl;
using mojo::StrongBindingSet;

using namespace intelligence;

struct ContextEntry {
  ContextUpdate latest;
  InterfacePtrSet<ContextPublisherController> publisher_controllers;
  StrongBindingSet<ContextPublisherLink> publishers;
  std::vector<ContextSubscriberLinkPtr> subscribers;
};

// label => schema => entry
typedef std::map<std::string, std::map<std::string, ContextEntry>> ContextRepo;

class ContextPublisherLinkImpl : public ContextPublisherLink {
 public:
  ContextPublisherLinkImpl(const std::string& whoami, ContextEntry* entry):
      whoami_(whoami), entry_(entry) {}

  void Update(const mojo::String& json_value) override {
    entry_->latest.source = whoami_;
    entry_->latest.json_value = json_value;

    for (const ContextSubscriberLinkPtr& subscriber : entry_->subscribers) {
      subscriber->OnUpdate(entry_->latest.Clone());
    }
  }

 private:
  const std::string whoami_;
  ContextEntry* entry_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(ContextPublisherLinkImpl);
};

template<typename Interface>
class ContextPublisherClient: public virtual Interface {
 public:
  ContextPublisherClient(const std::string& whoami, ContextRepo* repo):
      whoami_(whoami), repo_(repo) {}

  void Publish(const mojo::String& label, const mojo::String& schema,
               InterfaceHandle<ContextPublisherController> controller_handle,
               InterfaceRequest<ContextPublisherLink> link) override {
    ContextEntry& entry = (*repo_)[label][schema];

    if (controller_handle) {
      ContextPublisherControllerPtr controller =
          ContextPublisherControllerPtr::Create(controller_handle.Pass());

      // Immediately notify if there are already subscribers.
      if (!entry.subscribers.empty())
        controller->OnHasSubscribers();

      entry.publisher_controllers.AddInterfacePtr(controller.Pass());
    }

    entry.publishers.AddBinding(new ContextPublisherLinkImpl(whoami_, &entry),
                                link.Pass());
  }

 private:
  const std::string whoami_;
  ContextRepo* repo_;
};

template<typename Interface>
class ContextSubscriberClient: public virtual Interface {
 public:
  ContextSubscriberClient(ContextRepo* repo): repo_(repo) {}
  virtual ~ContextSubscriberClient() {}

  // TODO(rosswang): additional backpressure modes. For now, just do
  // on-backpressure-buffer, which is the default for Mojo. When we add
  // backpressure, we'll probably add a callback, or add a reactive pull API.
  virtual void Subscribe(const mojo::String& label, const mojo::String& schema,
                         InterfaceHandle<ContextSubscriberLink> link_handle) {
    ContextSubscriberLinkPtr link = ContextSubscriberLinkPtr::Create(
        link_handle.Pass());
    // TODO(rosswang): add a meta-query for whether any known publishers exist.
    ContextEntry& entry = (*repo_)[label][schema];

    // Taken from mojo::InterfacePtrSet; remove link on error.
    ContextSubscriberLink* ifc = link.get();
    link.set_connection_error_handler([&entry, ifc, label]{
      // General note: since Mojo message processing, including error handling,
      // is single-threaded, this is guaranteed not to happen at least until the
      // next processing loop.

      MOJO_LOG(VERBOSE) << "Subscription to " << label << " lost";

      auto it = std::find_if(entry.subscribers.begin(), entry.subscribers.end(),
                             [ifc](const ContextSubscriberLinkPtr& sub){
                               return sub.get() == ifc;
                             });

      assert(it != entry.subscribers.end());

      // Notify if this was the last subscriber.
      if (entry.subscribers.size() == 1) {
        MOJO_LOG(VERBOSE) << "No more subscribers to " << label;
        entry.publisher_controllers.ForAllPtrs(
            [](ContextPublisherController* controller) {
              controller->OnNoSubscribers();
            });
      }

      // This must be the last line in the error handler, because once we do
      // this the lambda is destroyed and subsequent capture accesses seg-fault.
      entry.subscribers.erase(it);
    });

    // If there is already context, send it as an initial update. If it could
    // be stale, it is up to the publisher to have removed it.
    if (entry.latest.json_value)
      link->OnUpdate(entry.latest.Clone());

    // Notify if this is the first subscriber.
    if (entry.subscribers.empty()) {
      entry.publisher_controllers.ForAllPtrs(
          [](ContextPublisherController* controller) {
            controller->OnHasSubscribers();
          });
    }

    entry.subscribers.emplace_back(link.Pass());
  }

 private:
  ContextRepo* repo_;
};

typedef ContextPublisherClient<ContextAcquirerClient> ContextAcquirerClientImpl;

class ContextAgentClientImpl:
    public ContextSubscriberClient<ContextAgentClient>,
    public ContextPublisherClient<ContextAgentClient> {
 public:
  ContextAgentClientImpl(const std::string& whoami, ContextRepo* repo_):
      ContextSubscriberClient(repo_), ContextPublisherClient(whoami, repo_) {}
};

typedef ContextSubscriberClient<SuggestionAgentClient>
    SuggestionAgentClientImpl;

class ContextServiceApp : public ApplicationImplBase {
 public:
  ContextServiceApp() {}

  bool OnAcceptConnection(ServiceProviderImpl* service_provider_impl) override {
    service_provider_impl->AddService<ContextAcquirerClient>(
        [this](const ConnectionContext& connection_context,
               InterfaceRequest<ContextAcquirerClient> request) {
          caq_clients_.AddBinding(new ContextAcquirerClientImpl(
              connection_context.remote_url, &repo_), request.Pass());
        });
    service_provider_impl->AddService<ContextAgentClient>(
        [this](const ConnectionContext& connection_context,
               InterfaceRequest<ContextAgentClient> request) {
          cag_clients_.AddBinding(new ContextAgentClientImpl(
              connection_context.remote_url, &repo_), request.Pass());
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
  ContextRepo repo_;
  StrongBindingSet<ContextAcquirerClient> caq_clients_;
  StrongBindingSet<ContextAgentClient> cag_clients_;
  StrongBindingSet<SuggestionAgentClient> sag_clients_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(ContextServiceApp);
};

} // namespace

MojoResult MojoMain(MojoHandle request) {
  ContextServiceApp app;
  return mojo::RunApplication(request, &app);
}
