// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_HOSTING_SINGLETON_FIDL_SERVER_H_
#define SRC_MEDIA_VNEXT_LIB_HOSTING_SINGLETON_FIDL_SERVER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include "src/media/vnext/lib/hosting/service_provider.h"
#include "src/media/vnext/lib/threads/thread.h"

namespace fmlib {

// Base class for FIDL servers who serve multiple clients with the same instance. The server
// can run on any thread for which the caller has a |Thread|.
template <typename Interface>
class SingletonFidlServer : public Interface {
 public:
  using Creator = fit::function<std::unique_ptr<SingletonFidlServer<Interface>>(Thread)>;

  // Registers a |SingletonFidlServer| with |service_provider|. When initially bound, the server
  // will be instantiated and run on |thread|. If |destroy_when_unbound| is true, the server will
  // be destroyed when it no longer has clients. Ownership of |thread| is maintained by the binder
  // so it can be reused when the server needs to be instantiated again.
  static void Register(ServiceProvider& service_provider, Thread thread, Creator creator,
                       bool destroy_when_unbound) {
    service_provider.RegisterService(
        Interface::Name_,
        std::make_unique<Binder>(std::move(thread), std::move(creator), destroy_when_unbound));
  }

  // Registers a |SingletonFidlServer| with |service_provider|. When initially bound, the server
  // will be instantiated and run on a new thread named |thread_name|. If |destroy_when_unbound| is
  // true, the server will be destroyed when it no longer has clients. When this happens, the
  // created thread is released, and a new one with the same name is created when the server needs
  // to be instantiated again.
  static void Register(ServiceProvider& service_provider, const char* thread_name, Creator creator,
                       bool destroy_when_unbound) {
    service_provider.RegisterService(
        Interface::Name_,
        std::make_unique<Binder>(thread_name, std::move(creator), destroy_when_unbound));
  }

  virtual ~SingletonFidlServer() = default;

 protected:
  const fidl::BindingSet<Interface>& binding_set() const { return binding_set_; }
  fidl::BindingSet<Interface>& binding_set() { return binding_set_; }

  // Causes deferred binding to complete if the constructor was called with a false |bind_now|
  // parameter. This method must not be called if |bind_now| was true and may only be called once
  // if |bind_now| was false.
  void CompleteDeferredBinding() {
    FX_CHECK(bind_bridge_.completer);
    bind_bridge_.completer.complete_ok();
  }

  // Add a binding to this server.
  void AddBinding(fidl::InterfaceRequest<Interface> request) {
    FX_CHECK(request);

    binding_set_.AddBinding(this, std::move(request));
  }

  // Returns a promise that completes when the binding set becomes empty.
  [[nodiscard]] fpromise::promise<> WhenBindingSetEmpty() {
    fpromise::bridge<> bridge;
    binding_set_.set_empty_set_handler(bridge.completer.bind());

    return bridge.consumer.promise();
  }

  // Constructs a |SingletonFidlServer|. If |bind_now| is true, clients will be bound immediately.
  // Otherwise, the clients will not be bound until |CompleteDeferredBinding| is called.
  explicit SingletonFidlServer(bool bind_now = true) {
    if (bind_now) {
      bind_bridge_.completer.complete_ok();
    }
  }

 private:
  class Binder : public ServiceBinder {
   public:
    Binder(Thread thread, Creator creator, bool destroy_when_unbound)
        : thread_(std::move(thread)),
          creator_(std::move(creator)),
          destroy_when_unbound_(destroy_when_unbound) {
      FX_CHECK(thread_);
      FX_CHECK(creator_);
    }

    Binder(const char* thread_name, Creator creator, bool destroy_when_unbound)
        : thread_name_(thread_name),
          creator_(std::move(creator)),
          destroy_when_unbound_(destroy_when_unbound) {
      FX_CHECK(thread_name_);
      FX_CHECK(creator_);
    }

    ~Binder() override = default;

    void Bind(zx::channel channel) override {
      if (!thread_) {
        thread_ = Thread::CreateNewThread(thread_name_);
      }

      thread_.PostTask([this, channel = std::move(channel)]() mutable {
        if (!server_) {
          server_ = creator_(thread_);
          if (!server_) {
            return;
          }

          thread_.schedule_task(server_->bind_bridge_.consumer.promise().and_then([this]() mutable {
            ready_to_bind_ = true;
            for (auto& channel : pending_binds_) {
              server_->AddBinding(fidl::InterfaceRequest<Interface>(std::move(channel)));
            }

            pending_binds_.clear();
          }));

          if (destroy_when_unbound_) {
            thread_.schedule_task(server_->WhenBindingSetEmpty().and_then([this]() {
              server_ = nullptr;

              if (thread_name_) {
                // |thread_| was created for this server instance. We'll create a new one if we
                // need to instantiate again.
                thread_ = nullptr;
              }
            }));
          }
        }

        if (ready_to_bind_) {
          server_->AddBinding(fidl::InterfaceRequest<Interface>(std::move(channel)));
        } else {
          pending_binds_.push_back(std::move(channel));
        }
      });
    }

   private:
    const char* thread_name_ = nullptr;
    Thread thread_;
    Creator creator_;
    bool destroy_when_unbound_;
    std::unique_ptr<SingletonFidlServer<Interface>> server_;
    bool ready_to_bind_ = false;
    std::vector<zx::channel> pending_binds_;
  };

  fpromise::bridge<> bind_bridge_;
  fidl::BindingSet<Interface> binding_set_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_HOSTING_SINGLETON_FIDL_SERVER_H_
