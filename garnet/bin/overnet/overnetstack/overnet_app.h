// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/channel.h>
#include <memory>
#include "garnet/lib/overnet/endpoint/router_endpoint.h"
#include "garnet/lib/overnet/environment/timer.h"
#include "lib/component/cpp/startup_context.h"

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
  class ServiceProvider {
   public:
    virtual ~ServiceProvider() {}
    virtual void Connect(const fuchsia::overnet::protocol::Introduction& intro,
                         zx::channel channel) = 0;
  };

  // Register a service provider for this app.
  void RegisterServiceProvider(const std::string& name,
                               std::unique_ptr<ServiceProvider> provider);

  // Bind 'channel' to a local overnet service.
  void ConnectToLocalService(
      const fuchsia::overnet::protocol::Introduction& intro,
      zx::channel channel);

  /////////////////////////////////////////////////////////////////////////////
  // Accessors for well known objects.

  overnet::RouterEndpoint* endpoint() { return &endpoint_; }
  component::StartupContext* startup_context() const {
    return startup_context_.get();
  }
  overnet::Timer* timer() { return timer_; }
  overnet::NodeId node_id() const { return node_id_; }

  /////////////////////////////////////////////////////////////////////////////

  // Bind together an overnet stream and a zx::channel and keep them
  // communicating until one side closes.
  void BindStream(overnet::RouterEndpoint::NewStream ns, zx::channel channel);

 private:
  void ReadNextIntroduction();
  void UpdateDescription();

  static overnet::NodeId GenerateNodeId();

  const std::unique_ptr<component::StartupContext> startup_context_;
  overnet::Timer* const timer_;
  const overnet::NodeId node_id_ = GenerateNodeId();
  overnet::RouterEndpoint endpoint_{timer_, node_id_, true};
  std::vector<std::unique_ptr<Actor>> actors_;
  std::unordered_map<std::string, std::unique_ptr<ServiceProvider>>
      service_providers_;
};

}  // namespace overnetstack
