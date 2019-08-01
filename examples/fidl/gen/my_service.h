// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FIDL_GEN_MY_SERVICE_H_
#define EXAMPLES_FIDL_GEN_MY_SERVICE_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fidl-service/cpp/member_connector.h>
#include <lib/fidl-service/cpp/service_handler.h>

#include <fidl/examples/echo/cpp/fidl.h>

// Example of generated code.
//
// TODO(FIDL-713): Replace with generated code when bindings are ready.

namespace fuchsia {
namespace examples {

class MyService final {
 public:
  struct Handler;

  static constexpr char Name[] = "fuchsia.examples.MyService";

  explicit MyService(fidl::InterfaceHandle<fuchsia::io::Directory> dir) : dir_(std::move(dir)) {}

  explicit operator bool() const { return dir_.is_valid(); }

  fidl::MemberConnector<fidl::examples::echo::Echo> foo() const {
    return fidl::MemberConnector<fidl::examples::echo::Echo>(dir_, "foo");
  }

  fidl::MemberConnector<fidl::examples::echo::Echo> bar() const {
    return fidl::MemberConnector<fidl::examples::echo::Echo>(dir_, "bar");
  }

 private:
  fidl::InterfaceHandle<fuchsia::io::Directory> dir_;
};

struct MyService::Handler final : public fidl::ServiceHandler {
  zx_status_t add_foo(fidl::InterfaceRequestHandler<fidl::examples::echo::Echo> handler) {
    return AddMember("foo", std::move(handler));
  }

  zx_status_t add_bar(fidl::InterfaceRequestHandler<fidl::examples::echo::Echo> handler) {
    return AddMember("bar", std::move(handler));
  }
};

}  // namespace examples
}  // namespace fuchsia

#endif  // EXAMPLES_FIDL_GEN_MY_SERVICE_H_
