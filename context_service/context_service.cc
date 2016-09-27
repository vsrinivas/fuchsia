// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/maxwell/context_service/context_service.mojom.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/bindings/interface_ptr_set.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace {

using mojo::ApplicationImplBase;
using mojo::BindingSet;
using mojo::ConnectionContext;
using mojo::InterfaceHandle;
using mojo::InterfacePtrSet;
using mojo::InterfaceRequest;
using mojo::ServiceProviderImpl;

using namespace intelligence;

struct ContextEntry {
  ContextEntry() {
    latest.source = "";
    latest.json_value = "";
  }

  ContextUpdate latest;
  InterfacePtrSet<ContextListener> listeners;
};

typedef std::map<std::string, ContextEntry> ContextRepo;

class PublisherPipeImpl : public PublisherPipe {
 public:
  PublisherPipeImpl(const mojo::String& whoami, ContextRepo& repo,
                    InterfaceRequest<PublisherPipe> handle)
      : strong_binding_(this, handle.Pass()), whoami_(whoami), repo_(repo) {}

  ~PublisherPipeImpl() {
    MOJO_LOG(INFO) << "publisher " << whoami_ << " terminated";
  }

  void Publish(const mojo::String& label,
               const mojo::String& json_value) override {
    MOJO_LOG(INFO) << "publisher " << whoami_ << " set value "
                   << label << ": " << json_value;

    ContextEntry& entry = repo_[label];
    entry.latest.source = whoami_;
    entry.latest.json_value = json_value;

    entry.listeners.ForAllPtrs([&entry](auto listener) {
      listener->OnUpdate(entry.latest.Clone());
    });
  }

 private:
  mojo::StrongBinding<PublisherPipe> strong_binding_;
  mojo::String whoami_;
  ContextRepo& repo_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(PublisherPipeImpl);
};

class ContextServiceImpl : public ContextPublisher, public ContextSubscriber {
 public:
  ContextServiceImpl() {}

  void StartPublishing(const mojo::String& whoami,
                       InterfaceRequest<PublisherPipe> pipe) override {
    MOJO_LOG(INFO) << "StartPublishing " << whoami;

    new PublisherPipeImpl(whoami, repo, pipe.Pass());
  }

  // TODO(rosswang): additional backpressure modes. For now, just do
  // on-backpressure-buffer, which is the default for Mojo. When we add
  // backpressure, we'll probably add a callback, or add a reactive pull API.
  void Subscribe(const mojo::String& label,
                 InterfaceHandle<ContextListener> listener_handle) override {
    MOJO_LOG(INFO) << "Subscribe to " << label;

    ContextListenerPtr listener = ContextListenerPtr::Create(
        listener_handle.Pass());

    ContextEntry& entry = repo[label];
    listener->OnUpdate(entry.latest.Clone());
    entry.listeners.AddInterfacePtr(listener.Pass());
  }

 private:
  ContextRepo repo;
  MOJO_DISALLOW_COPY_AND_ASSIGN(ContextServiceImpl);
};

class ContextServiceApp : public ApplicationImplBase {
 public:
  ContextServiceApp() {}

  bool OnAcceptConnection(ServiceProviderImpl* service_provider_impl) override {
    // Singleton service
    service_provider_impl->AddService<ContextPublisher>(
        [this](const ConnectionContext& connection_context,
               InterfaceRequest<ContextPublisher> request) {
          pub_bindings_.AddBinding(&cxs_impl_, request.Pass());
        });
    service_provider_impl->AddService<ContextSubscriber>(
        [this](const ConnectionContext& connection_context,
               InterfaceRequest<ContextSubscriber> request) {
          sub_bindings_.AddBinding(&cxs_impl_, request.Pass());
        });
    return true;
  }

 private:
  ContextServiceImpl cxs_impl_;
  BindingSet<ContextPublisher> pub_bindings_;
  BindingSet<ContextSubscriber> sub_bindings_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(ContextServiceApp);
};

} // namespace

MojoResult MojoMain(MojoHandle request) {
  ContextServiceApp app;
  return mojo::RunApplication(request, &app);
}
