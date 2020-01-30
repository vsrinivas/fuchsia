
// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_INTEGRATION_TESTS_ECHO_SERVER_H_
#define SRC_SYS_APPMGR_INTEGRATION_TESTS_ECHO_SERVER_H_

#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>

#include <fidl/examples/echo/cpp/fidl.h>

class EchoImpl : public fidl::examples::echo::Echo {
 public:
  void EchoString(fidl::StringPtr value, EchoStringCallback callback) override {
    callback(std::move(value));
  }
  fidl::InterfaceRequestHandler<fidl::examples::echo::Echo> GetHandler(
      async_dispatcher_t* dispatcher) {
    return bindings_.GetHandler(this, dispatcher);
  }

  void AddBinding(zx::channel request, async_dispatcher_t* dispatcher) {
    bindings_.AddBinding(
        this, fidl::InterfaceRequest<fidl::examples::echo::Echo>(std::move(request)), dispatcher);
  }

 private:
  fidl::BindingSet<fidl::examples::echo::Echo> bindings_;
};

#endif  // SRC_SYS_APPMGR_INTEGRATION_TESTS_ECHO_SERVER_H_
