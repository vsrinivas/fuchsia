// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_SERVICE_IMPL_BASE_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_SERVICE_IMPL_BASE_H_

#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

namespace mdns {

template <typename TProtocol>
class ServiceImplBase : public TProtocol {
 public:
  ServiceImplBase(Mdns& mdns, fidl::InterfaceRequest<TProtocol> request, fit::closure deleter)
      : mdns_(mdns), binding_(this, std::move(request)), deleter_(std::move(deleter)) {
    binding_.set_error_handler([this](zx_status_t status) mutable { Quit(); });
  }

  ~ServiceImplBase() override { Quit(); }

  // Disallow copy, assign and move.
  ServiceImplBase(const ServiceImplBase&) = delete;
  ServiceImplBase(ServiceImplBase&&) = delete;
  ServiceImplBase& operator=(const ServiceImplBase&) = delete;
  ServiceImplBase& operator=(ServiceImplBase&&) = delete;

 protected:
  Mdns& mdns() { return mdns_; }

  void Quit(zx_status_t status = ZX_ERR_PEER_CLOSED) {
    binding_.set_error_handler(nullptr);

    if (binding_.is_bound()) {
      binding_.Close(status);
    }

    if (deleter_) {
      auto deleter = std::move(deleter_);
      FX_DCHECK(!deleter_);
      deleter();
      // Do not attempt to dereference |this| after this point.
    }
  }

 private:
  Mdns& mdns_;
  fidl::Binding<TProtocol> binding_;
  fit::closure deleter_;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_SERVICE_IMPL_BASE_H_
