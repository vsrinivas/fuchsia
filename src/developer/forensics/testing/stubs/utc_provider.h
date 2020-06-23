// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_UTC_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_UTC_PROVIDER_H_

#include <fuchsia/time/cpp/fidl.h>
#include <fuchsia/time/cpp/fidl_test_base.h>
#include <lib/zx/time.h>

#include <vector>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics {
namespace stubs {

using UtcProviderBase = SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::time, Utc);

class UtcProvider : public UtcProviderBase {
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

  // |fuchsia::time::Utc|
  void WatchState(WatchStateCallback callback) override;

 private:
  bool Done();

  async_dispatcher_t* dispatcher_;
  std::vector<Response> responses_;
  std::vector<Response>::const_iterator next_reponse_;
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_UTC_PROVIDER_H_
