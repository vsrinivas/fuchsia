// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OVERNET_OVERNETSTACK_OVERNET_APP_H_
#define SRC_CONNECTIVITY_OVERNET_OVERNETSTACK_OVERNET_APP_H_

#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>

#include <memory>

#include "src/connectivity/overnet/lib/endpoint/router_endpoint.h"
#include "src/connectivity/overnet/lib/environment/timer.h"

namespace overnetstack {

// Main application object: provides common objects to Actors, which implement
// the bulk of the functionality of the app.
class OvernetApp final {
 public:
  OvernetApp(overnet::Timer* timer);
  ~OvernetApp();
  overnet::Status Start();

  /////////////////////////////////////////////////////////////////////////////
  // Some (usually asynchronous) service that mutates application state.
  class Actor {
   public:
    virtual ~Actor() {}
    virtual overnet::Status Start() = 0;
  };

  template <class T, class... Args>
  T* InstantiateActor(Args&&... args) {
    auto ptr = std::make_unique<T>(this, std::forward<Args>(args)...);
    auto* out = ptr.get();
    actors_.emplace_back(std::move(ptr));
    return out;
  }

  /////////////////////////////////////////////////////////////////////////////
  // Allows binding a zx::channel to some service denoted by an Introduction
  // object.
  class ServiceProvider;
  using ServiceProviderMap =
      std::unordered_map<std::string, std::unique_ptr<ServiceProvider>>;
  class ServiceProvider : public overnet::RouterEndpoint::Service {
    friend class OvernetApp;

   public:
    ServiceProvider(OvernetApp* app, std::string fully_qualified_name,
                    fuchsia::overnet::protocol::ReliabilityAndOrdering
                        reliability_and_ordering)
        : overnet::RouterEndpoint::Service(
              &app->endpoint_, fully_qualified_name, reliability_and_ordering),
          app_(app) {}
    virtual ~ServiceProvider() {}
    virtual void Connect(zx::channel channel) = 0;
    void AcceptStream(overnet::RouterEndpoint::NewStream stream) override final;

   protected:
    void Close() { app_->service_providers_.erase(where_am_i_); };

   private:
    OvernetApp* const app_;
    ServiceProviderMap::iterator where_am_i_;
  };

  template <class T, class... Args>
  void InstantiateServiceProvider(Args&&... args) {
    auto sp = std::make_unique<T>(std::forward<Args>(args)...);
    auto name = sp->fully_qualified_name;
    // Keep pointer to sp even though we'll move it as an arg on the next line,
    // so we can assign where_am_i_
    auto sp_ptr = sp.get();
    sp_ptr->where_am_i_ =
        service_providers_.emplace(std::move(name), std::move(sp)).first;
  }

  // Bind 'channel' to a local overnet service.
  void ConnectToLocalService(const std::string& service_name,
                             zx::channel channel);

  /////////////////////////////////////////////////////////////////////////////
  // Accessors for well known objects.

  overnet::RouterEndpoint* endpoint() { return &endpoint_; }
  sys::ComponentContext* component_context() const {
    return component_context_.get();
  }
  overnet::Timer* timer() { return timer_; }
  overnet::NodeId node_id() const { return node_id_; }

  /////////////////////////////////////////////////////////////////////////////

  // Bind together an overnet stream and a zx::channel and keep them
  // communicating until one side closes.
  void BindChannel(overnet::RouterEndpoint::NewStream ns, zx::channel channel);
  // Similarly, for zx::socket
  void BindSocket(overnet::RouterEndpoint::NewStream ns, zx::socket socket);

 private:
  static overnet::NodeId GenerateNodeId();

  const std::unique_ptr<sys::ComponentContext> component_context_;
  overnet::Timer* const timer_;
  const overnet::NodeId node_id_ = GenerateNodeId();
  overnet::RouterEndpoint endpoint_{timer_, node_id_, true};
  std::vector<std::unique_ptr<Actor>> actors_;
  ServiceProviderMap service_providers_;
};

}  // namespace overnetstack

#endif  // SRC_CONNECTIVITY_OVERNET_OVERNETSTACK_OVERNET_APP_H_
