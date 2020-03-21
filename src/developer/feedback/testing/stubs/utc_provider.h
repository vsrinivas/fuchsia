// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_UTC_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_UTC_PROVIDER_H_

#include <fuchsia/time/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/zx/time.h>

#include <vector>

namespace feedback {
namespace stubs {

// Stub fuchsia.time.Utc service that returns canned responses for
// fuchsia::time::Utc::WatchState().
class UtcProvider : public fuchsia::time::Utc {
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

  // Returns a request handler for binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::time::Utc> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::time::Utc> request) {
      binding_ = std::make_unique<fidl::Binding<fuchsia::time::Utc>>(this, std::move(request));
    };
  }

  void WatchState(WatchStateCallback callback) override;

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
