// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by app BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_INTL_WISDOM_SERVER_INTL_WISDOM_SERVER_IMPL_H_
#define GARNET_EXAMPLES_INTL_WISDOM_SERVER_INTL_WISDOM_SERVER_IMPL_H_

#include <fuchsia/examples/intl/wisdom/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "third_party/icu/source/common/unicode/locid.h"
#include "third_party/icu/source/i18n/unicode/calendar.h"

namespace intl_wisdom {

// Implementation of the |IntlWisdomServer| service interface.
//
// Starts a FIDL service, initializes the ICU library, and responds to calls to
// |AskForWisdom| with pithy multilingual remarks.
class IntlWisdomServerImpl : fuchsia::examples::intl::wisdom::IntlWisdomServer {
 public:
  IntlWisdomServerImpl(std::unique_ptr<sys::ComponentContext> context);

  // Responds with a multilingual string, using locales from the given
  // |intl_profile|.
  virtual void AskForWisdom(
      fuchsia::intl::Profile intl_profile, int64_t timestamp_ms,
      fuchsia::examples::intl::wisdom::IntlWisdomServer::AskForWisdomCallback
          callback) override;

 private:
  IntlWisdomServerImpl(const IntlWisdomServerImpl&) = delete;
  IntlWisdomServerImpl& operator=(const IntlWisdomServerImpl&) = delete;

  // Generates the actual response string.
  std::string BuildResponse(
      const long timestamp_ms, const std::vector<icu::Locale>& locales,
      const std::vector<std::unique_ptr<icu::Calendar>>& calendars) const;

  std::unique_ptr<sys::ComponentContext> startup_context_;
  fidl::BindingSet<fuchsia::examples::intl::wisdom::IntlWisdomServer> bindings_;
};

}  // namespace intl_wisdom

#endif  // GARNET_EXAMPLES_INTL_WISDOM_SERVER_INTL_WISDOM_SERVER_IMPL_H_
