// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FIDL_GEN_MY_SERVICE_LLCPP_H_
#define EXAMPLES_FIDL_GEN_MY_SERVICE_LLCPP_H_

#include <lib/fidl/llcpp/connect_service.h>
#include <lib/fidl/llcpp/string_view.h>

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#endif  // __Fuchsia__

#include "fidl/examples/echo/llcpp/fidl.h"

namespace llcpp::fuchsia::examples {

#ifdef __Fuchsia__

class MyService final {
  MyService() = default;

 public:
  static constexpr char Name[] = "fuchsia.examples.MyService";

  class ServiceClient final {
    ServiceClient() = delete;

   public:
    ServiceClient(zx::channel dir, ::fidl::internal::ConnectMemberFunc connect_func)
        : dir_(std::move(dir)), connect_func_(connect_func) {}

    ::fidl::result<::fidl::ClientChannel<::llcpp::fidl::examples::echo::Echo>> ConnectFoo() {
      zx::channel local, remote;
      zx_status_t result = zx::channel::create(0, &local, &remote);
      if (result != ZX_OK) {
        return ::fit::error(result);
      }
      result =
          connect_func_(zx::unowned_channel(dir_), ::fidl::StringView("foo"), std::move(remote));
      if (result != ZX_OK) {
        return ::fit::error(result);
      }
      return ::fit::ok(
          ::fidl::ClientChannel<::llcpp::fidl::examples::echo::Echo>(std::move(local)));
    }

    ::fidl::result<::fidl::ClientChannel<::llcpp::fidl::examples::echo::Echo>> ConnectBar() {
      zx::channel local, remote;
      zx_status_t result = zx::channel::create(0, &local, &remote);
      if (result != ZX_OK) {
        return ::fit::error(result);
      }
      result =
          connect_func_(zx::unowned_channel(dir_), ::fidl::StringView("bar"), std::move(remote));
      if (result != ZX_OK) {
        return ::fit::error(result);
      }
      return ::fit::ok(
          ::fidl::ClientChannel<::llcpp::fidl::examples::echo::Echo>(std::move(local)));
    }

   private:
    zx::channel dir_;
    ::fidl::internal::ConnectMemberFunc connect_func_;
  };
};

#endif  // __Fuchsia__

}  // namespace llcpp::fuchsia::examples

#endif  // EXAMPLES_FIDL_GEN_MY_SERVICE_LLCPP_H_
