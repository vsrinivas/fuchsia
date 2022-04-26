// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_HOSTING_NONCE_FIDL_SERVER_H_
#define SRC_MEDIA_VNEXT_LIB_HOSTING_NONCE_FIDL_SERVER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include "src/media/vnext/lib/hosting/service_provider.h"
#include "src/media/vnext/lib/threads/thread.h"

namespace fmlib {

// Base class for FIDL servers whose lifetime is scoped to the client connection. That is,
// each client gets its own 'nonce' server instance.
template <typename Interface>
class NonceFidlServer : public Interface {
 public:
  using Creator = fit::function<std::unique_ptr<NonceFidlServer<Interface>>(Thread)>;

  // Launches a |NonceFidlServer|, binding |request|. The launched instance serves only one
  // client.
  static void Launch(Thread thread, fidl::InterfaceRequest<Interface> request, Creator creator) {
    Binder binder(std::move(thread), std::move(creator));
    binder.Bind(request.TakeChannel());
  }

  // Registers a |NonceFidlServer| with |service_provider|. When initially bound, the servers
  // will be instantiated and run on |thread|.
  static void Register(ServiceProvider& service_provider, Thread thread, Creator creator) {
    service_provider.RegisterService(
        Interface::Name_, std::make_unique<Binder>(std::move(thread), std::move(creator)));
  }

  // Registers a |NonceFidlServer| with |service_provider|. When initially bound, each server
  // will be instantiated and run on its own new thread named |thread_name|.
  static void Register(ServiceProvider& service_provider, const char* thread_name,
                       Creator creator) {
    service_provider.RegisterService(std::string(Interface::Name_),
                                     std::make_unique<Binder>(thread_name, std::move(creator)));
  }

  virtual ~NonceFidlServer() = default;

 protected:
  const fidl::Binding<Interface>& binding() const { return binding_; }

  fidl::Binding<Interface>& binding() { return binding_; }

  typename Interface::EventSender_& events() { return binding_.events(); }

  // Causes deferred binding to complete if the constructor was called with a false |bind_now|
  // parameter. This method must not be called if |bind_now| was true and may only be called once
  // if |bind_now| was false.
  void CompleteDeferredBinding() {
    FX_CHECK(bind_bridge_.completer);
    bind_bridge_.completer.complete_ok();
  }

  // Unbinds this server, if this server is bound. This results in this server being deleted on
  // its designated thread.
  void Unbind(zx_status_t status) {
    if (binding_.is_bound()) {
      binding_.Close(status);
    }

    binding_.set_error_handler(nullptr);

    if (unbind_completer_) {
      unbind_completer_.complete_ok(status);
    }
  }

  // Constructs a |NonceFidlServer|. If |bind_now| is true, the client will be bound immediately.
  // Otherwise, the client will not be bound until |CompleteDeferredBinding| is called.
  explicit NonceFidlServer(bool bind_now = true) : binding_(this) {
    if (bind_now) {
      bind_bridge_.completer.complete_ok();
    }
  }

 private:
  class Binder : public ServiceBinder {
   public:
    Binder(Thread thread, Creator creator)
        : thread_(std::move(thread)), creator_(std::move(creator)) {
      FX_CHECK(thread_);
      FX_CHECK(creator_);
    }

    Binder(const char* thread_name, Creator creator)
        : thread_name_(thread_name), creator_(std::move(creator)) {
      FX_CHECK(thread_name_);
      FX_CHECK(creator_);
    }

    ~Binder() override = default;

    // Launches the server and binds |channel| to it. This |Binder| may be destroyed immediately
    // after this method returns without interfering with the operation of this method.
    void Bind(zx::channel channel) override {
      FX_CHECK(thread_ || thread_name_);
      Thread thread = thread_ ? std::move(thread_) : Thread::CreateNewThread(thread_name_);

      // Note: Do not capture |this| here, because we allow |this| to be destroyed after this
      // method returns.
      thread.PostTask([thread, channel = std::move(channel), creator = creator_.share()]() mutable {
        auto server = creator(thread);
        if (!server) {
          return;
        }

        auto raw_server = server.get();
        thread.schedule_task(raw_server->bind_bridge_.consumer.promise().and_then(
            [thread, server = std::move(server), channel = std::move(channel)]() mutable {
              auto raw_server = server.get();
              return raw_server->Bind(fidl::InterfaceRequest<Interface>(std::move(channel)))
                  .and_then([thread, server = std::move(server)](zx_status_t& status) {
                    // |server| goes out of scope here, deleting the nonce server. If |thread|
                    // is otherwise unreferenced, it will be deleted as well. The service may or
                    // may not maintain a reference to |thread|, so it's important to reference
                    // it from the capture list.
                  });
            }));
      });
    }

   private:
    const char* thread_name_ = nullptr;
    Thread thread_;
    Creator creator_;
  };

  // Returns a promise that binds to this server and completes when this server is unbound.
  [[nodiscard]] fpromise::promise<zx_status_t> Bind(fidl::InterfaceRequest<Interface> request) {
    FX_CHECK(request);
    binding_.Bind(std::move(request));
    fpromise::bridge<zx_status_t> unbind_bridge;
    unbind_completer_ = std::move(unbind_bridge.completer);
    binding_.set_error_handler(fit::bind_member(this, &NonceFidlServer::Unbind));

    return unbind_bridge.consumer.promise();
  }

  fidl::Binding<Interface> binding_;
  fpromise::bridge<> bind_bridge_;
  fpromise::completer<zx_status_t> unbind_completer_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_HOSTING_NONCE_FIDL_SERVER_H_
