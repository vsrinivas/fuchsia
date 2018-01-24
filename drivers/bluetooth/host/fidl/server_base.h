// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace btlib {
namespace gap {

class Adapter;

}  // namespace gap
}  // namespace btlib

namespace bthost {

// This class acts as a common base type for all FIDL interface servers. Its
// main purpose is to provide type erasure for the ServerBase template below.
class Server {
 public:
  virtual ~Server() = default;
};

// ServerBase is a common base implementation for FIDL interface servers.
template <typename Interface>
class ServerBase : public Server, public Interface {
 public:
  // Constructs a FIDL server by binding a fidl::InterfaceRequest.
  ServerBase(fxl::WeakPtr<::btlib::gap::Adapter> adapter,
             Interface* impl,
             fidl::InterfaceRequest<Interface> request)
      : ServerBase(adapter, impl, request.PassChannel()) {}

  // Constructs a FIDL server by binding a zx::channel.
  ServerBase(fxl::WeakPtr<::btlib::gap::Adapter> adapter,
             Interface* impl,
             zx::channel channel)
      : binding_(impl, std::move(channel)), adapter_(adapter) {
    FXL_DCHECK(binding_.is_bound());
    FXL_DCHECK(adapter_);
  }

  ~ServerBase() override = default;

  void set_error_handler(std::function<void()> handler) {
    binding_.set_error_handler(std::move(handler));
  }

 protected:
  ::btlib::gap::Adapter* adapter() const { return adapter_.get(); }

 private:
  // Holds the channel from the FIDL client.
  ::fidl::Binding<Interface> binding_;

  // The underlying library Adapter.
  fxl::WeakPtr<::btlib::gap::Adapter> adapter_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServerBase);
};

}  // namespace bthost
