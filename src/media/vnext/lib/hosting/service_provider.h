// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_HOSTING_SERVICE_PROVIDER_H_
#define SRC_MEDIA_VNEXT_LIB_HOSTING_SERVICE_PROVIDER_H_

#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>

#include <string>
#include <unordered_map>

#include "src/media/vnext/lib/threads/thread.h"

namespace fmlib {

class ServiceBinder {
 public:
  ServiceBinder() = default;
  virtual ~ServiceBinder() = default;

  virtual void Bind(zx::channel channel) = 0;
};

// |ServiceProvider| is a registery for FIDL service implementations that launches service
// implementations as needed in response to |ConnectToService| calls. Currently, a service
// provider can optionally enlist a component context to find services that aren't locally
// registered, but it cannot expose outgoing services. That feature must be added.
class ServiceProvider {
 public:
  // Constructs a |ServiceProvider| that consults |component_context| for protocols not
  // registered locally. Must be called on |thread|, referred to henceforth as 'the constructor
  // thread'.
  ServiceProvider(sys::ComponentContext& component_context, Thread thread)
      : component_context_(&component_context), thread_(thread) {
    FX_CHECK(thread_.is_current());
  }

  // Constructs a |ServiceProvider|. Must be called on |thread|, referred to henceforth as 'the
  // constructor thread'.
  explicit ServiceProvider(Thread thread) : component_context_(nullptr), thread_(thread) {
    FX_CHECK(thread_.is_current());
  }

  ~ServiceProvider() = default;

  // Disallow copy, assign and move.
  ServiceProvider(const ServiceProvider&) = delete;
  ServiceProvider& operator=(const ServiceProvider&) = delete;
  ServiceProvider(ServiceProvider&&) = delete;
  ServiceProvider& operator=(ServiceProvider&&) = delete;

  // Registers a service. Must be called on the constructor thread.
  void RegisterService(std::string, std::unique_ptr<ServiceBinder> binder);

  // Clears all registered services. Must be called on the constructor thread.
  void ClearRegisteredServices() {
    FX_CHECK(thread_.is_current());
    binderies_by_protocol_name_.clear();
  }

  // Connects to a service. May be called on any thread.
  void ConnectToService(const std::string& protocol_name, zx::channel channel);

  // Connects to a service. May be called on any thread.
  template <typename Interface>
  inline fidl::InterfacePtr<Interface> ConnectToService(
      const std::string& protocol_name = Interface::Name_) {
    fidl::InterfacePtr<Interface> client;
    auto request = client.NewRequest();
    ConnectToService(protocol_name, request.TakeChannel());
    return client;
  }

 private:
  sys::ComponentContext* component_context_;
  Thread thread_;
  std::unordered_map<std::string, std::unique_ptr<ServiceBinder>> binderies_by_protocol_name_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_HOSTING_SERVICE_PROVIDER_H_
