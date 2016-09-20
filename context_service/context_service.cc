// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/maxwell/context_service/context_service.mojom.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace {

using mojo::ApplicationImplBase;
using mojo::BindingSet;
using mojo::InterfaceRequest;
using mojo::ServiceProviderImpl;

using intelligence::ContextPublisher;
using intelligence::PublisherPipe;
using intelligence::Status;

class PublisherPipeImpl : public PublisherPipe {
 public:
  PublisherPipeImpl(const mojo::String& whoami,
                    InterfaceRequest<PublisherPipe> handle)
      : strong_binding_(this, handle.Pass()), whoami_(whoami) {}

  void Publish(const mojo::String& label, const mojo::String& value,
               const PublishCallback& callback) override {
    MOJO_LOG(INFO) << "context_service publisher " << whoami_ << " set value "
                   << label << ": " << value;
    callback.Run(Status::Ok);
  }

 private:
  mojo::StrongBinding<PublisherPipe> strong_binding_;
  mojo::String whoami_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(PublisherPipeImpl);
};

class ContextServiceImpl : public ContextPublisher {
 public:
  ContextServiceImpl() {}

  void StartPublishing(const mojo::String& whoami,
                       InterfaceRequest<PublisherPipe> pipe) override {
    MOJO_LOG(INFO) << "context_service StartPublishing " << whoami;

    new PublisherPipeImpl(whoami, pipe.Pass());
  }

 private:
  MOJO_DISALLOW_COPY_AND_ASSIGN(ContextServiceImpl);
};

class ContextServiceApp : public ApplicationImplBase {
 public:
  ContextServiceApp() {}

  bool OnAcceptConnection(ServiceProviderImpl* service_provider_impl) override {
    service_provider_impl->AddService<ContextPublisher>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<ContextPublisher> request) {
          // All channels will connect to this singleton object, so just
          // add the binding to our collection.
          bindings_.AddBinding(&cxs_impl_, request.Pass());
        });
    return true;
  }

 private:
  ContextServiceImpl cxs_impl_;
  mojo::BindingSet<ContextPublisher> bindings_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(ContextServiceApp);
};

} // namespace

MojoResult MojoMain(MojoHandle request) {
  ContextServiceApp app;
  return mojo::RunApplication(request, &app);
}
