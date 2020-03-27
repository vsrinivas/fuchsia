// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_UTC_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_UTC_PROVIDER_H_

#include <fuchsia/time/cpp/fidl.h>
#include <fuchsia/time/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/zx/time.h>

#include <vector>

#include "src/lib/fxl/logging.h"

namespace feedback {
namespace stubs {

class UtcProvider : public fuchsia::time::testing::Utc_TestBase {
 public:
  struct Response {
    enum class Value {
      kBackstop,
      kExternal,
      kNoResponse,
    };

    constexpr explicit Response(Value value) : value(value), delay(zx::nsec(0)) {}

    constexpr Response(Value value, zx::duration delay) : value(value), delay(delay) {}

    Value value;
    zx::duration delay;
  };

  UtcProvider(async_dispatcher_t* dispatcher, const std::vector<Response>& responses)
      : dispatcher_(dispatcher), responses_(responses) {
    next_reponse_ = responses_.cbegin();
  }

  ~UtcProvider();

  fidl::InterfaceRequestHandler<fuchsia::time::Utc> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::time::Utc> request) {
      binding_ = std::make_unique<fidl::Binding<fuchsia::time::Utc>>(this, std::move(request));
    };
  }

  // |fuchsia::time::Utc|
  void WatchState(WatchStateCallback callback) override;

  // |fuchsia::time::testing::Utc_TestBase|
  void NotImplemented_(const std::string& name) override {
    FXL_NOTIMPLEMENTED() << name << " is not implemented";
  }

 private:
  bool Done();

  async_dispatcher_t* dispatcher_;
  std::unique_ptr<fidl::Binding<fuchsia::time::Utc>> binding_;
  std::vector<Response> responses_;
  std::vector<Response>::const_iterator next_reponse_;
};

}  // namespace stubs
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_UTC_PROVIDER_H_
