// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FIDL_GEN_MY_SERVICE_H_
#define EXAMPLES_FIDL_GEN_MY_SERVICE_H_

#include <lib/fidl/cpp/internal/header.h>

#include <fidl/examples/echo/cpp/fidl.h>

// Example of generated code.
//
// TODO(FIDL-713): Replace with generated code when bindings are ready.

namespace fuchsia {
namespace examples {

class MyService final {
 public:
  class Handler;

  static constexpr char Name[] = "fuchsia.examples.MyService";

  explicit MyService(std::unique_ptr<fidl::ServiceConnector> service)
      : service_(std::move(service)) {}

  explicit operator bool() const { return !!service_; }

  fidl::MemberConnector<fidl::examples::echo::Echo> foo() const {
    return fidl::MemberConnector<fidl::examples::echo::Echo>(service_.get(), "foo");
  }

  fidl::MemberConnector<fidl::examples::echo::Echo> bar() const {
    return fidl::MemberConnector<fidl::examples::echo::Echo>(service_.get(), "bar");
  }

 private:
  std::unique_ptr<fidl::ServiceConnector> service_;
};

class MyService::Handler final {
 public:
  explicit Handler(fidl::ServiceHandlerBase* service) : service_(service) {}

  zx_status_t add_foo(fidl::InterfaceRequestHandler<fidl::examples::echo::Echo> handler) {
    return service_->AddMember("foo", std::move(handler));
  }

  zx_status_t add_bar(fidl::InterfaceRequestHandler<fidl::examples::echo::Echo> handler) {
    return service_->AddMember("bar", std::move(handler));
  }

 private:
  fidl::ServiceHandlerBase* const service_;
};

}  // namespace examples
}  // namespace fuchsia

#endif  // EXAMPLES_FIDL_GEN_MY_SERVICE_H_
