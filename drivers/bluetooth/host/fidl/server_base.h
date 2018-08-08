// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_ptr.h>
#include <lib/fit/function.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace btlib {

namespace gap {
class Adapter;
}  // namespace gap

namespace gatt {
class GATT;
}  // namespace gatt

}  // namespace btlib

namespace bthost {

// This class acts as a common base type for all FIDL interface servers. Its
// main purpose is to provide type erasure for the ServerBase template below.
class Server {
 public:
  virtual ~Server() = default;

  virtual void set_error_handler(fit::closure handler) = 0;
};

// ServerBase is a common base implementation for FIDL interface servers.
template <typename Interface>
class ServerBase : public Server, public Interface {
 public:
  // Constructs a FIDL server by binding a fidl::InterfaceRequest.
  ServerBase(Interface* impl, fidl::InterfaceRequest<Interface> request)
      : ServerBase(impl, request.TakeChannel()) {}

  // Constructs a FIDL server by binding a zx::channel.
  ServerBase(Interface* impl, zx::channel channel)
      : binding_(impl, std::move(channel)) {
    FXL_DCHECK(binding_.is_bound());
  }

  ~ServerBase() override = default;

  void set_error_handler(fit::closure handler) override {
    binding_.set_error_handler(std::move(handler));
  }

 protected:
  ::fidl::Binding<Interface>* binding() { return &binding_; }

 private:
  // Holds the channel from the FIDL client.
  ::fidl::Binding<Interface> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServerBase);
};

// Base template for GAP FIDL interface servers. The GAP profile is accessible
// through an Adapter object.
template <typename Interface>
class AdapterServerBase : public ServerBase<Interface> {
 public:
  AdapterServerBase(fxl::WeakPtr<btlib::gap::Adapter> adapter, Interface* impl,
                    fidl::InterfaceRequest<Interface> request)
      : AdapterServerBase(adapter, impl, request.TakeChannel()) {}

  AdapterServerBase(fxl::WeakPtr<btlib::gap::Adapter> adapter, Interface* impl,
                    zx::channel channel)
      : ServerBase<Interface>(impl, std::move(channel)), adapter_(adapter) {
    FXL_DCHECK(adapter_);
  }

  ~AdapterServerBase() override = default;

 protected:
  btlib::gap::Adapter* adapter() const { return adapter_.get(); }

 private:
  fxl::WeakPtr<btlib::gap::Adapter> adapter_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AdapterServerBase);
};

// Base template for GATT FIDL interface servers. The GATT profile is accessible
// through an Adapter object.
template <typename Interface>
class GattServerBase : public ServerBase<Interface> {
 public:
  GattServerBase(fbl::RefPtr<btlib::gatt::GATT> gatt, Interface* impl,
                 fidl::InterfaceRequest<Interface> request)
      : ServerBase<Interface>(impl, std::move(request)), gatt_(gatt) {
    FXL_DCHECK(gatt_);
  }

  ~GattServerBase() override = default;

 protected:
  fbl::RefPtr<btlib::gatt::GATT> gatt() const { return gatt_; }

 private:
  fbl::RefPtr<btlib::gatt::GATT> gatt_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GattServerBase);
};

}  // namespace bthost
