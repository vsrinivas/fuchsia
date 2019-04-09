// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by app BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_INTL_CLIENT_INTL_WISDOM_CLIENT_H_
#define GARNET_EXAMPLES_INTL_CLIENT_INTL_WISDOM_CLIENT_H_

#include <lib/sys/cpp/component_context.h>

#include "fuchsia/examples/intl/wisdom/cpp/fidl.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

namespace intl_wisdom {

// A client class for communicating with an |IntlWisdomServer|.
//
// Call |Start| to request that a server be started. Then call |SendRequest| to
// ask the server for a wisdom string.
class IntlWisdomClient {
 public:
  IntlWisdomClient(std::unique_ptr<sys::ComponentContext> startup_context);

  const fuchsia::examples::intl::wisdom::IntlWisdomServerPtr& server() const {
    return server_;
  }

  // Asks the startup context's launcher to launch a server, and then connects
  // to the server.
  void Start(std::string server_url);
  // Sends a request for "wisdom" with given |timestamp| argument. The response,
  // if any, is provided via the |callback|.
  //
  // Params:
  //   timestamp: used for seeding the server's response
  //   time_zone: used in generating a |fuchsia::intl::Profile| for the request
  //   callback: async callback
  void SendRequest(
      zx::time timestamp, const icu::TimeZone& time_zone,
      fuchsia::examples::intl::wisdom::IntlWisdomServer::AskForWisdomCallback
          callback) const;

 private:
  IntlWisdomClient(const IntlWisdomClient&) = delete;
  IntlWisdomClient& operator=(const IntlWisdomClient&) = delete;

  std::unique_ptr<sys::ComponentContext> startup_context_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fuchsia::examples::intl::wisdom::IntlWisdomServerPtr server_;
};

}  // namespace intl_wisdom

#endif  // GARNET_EXAMPLES_INTL_CLIENT_INTL_WISDOM_CLIENT_H_
