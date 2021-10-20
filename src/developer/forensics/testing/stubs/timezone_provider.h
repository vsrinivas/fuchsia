// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_TIMEZONE_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_TIMEZONE_PROVIDER_H_

#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl_test_base.h>

#include <optional>
#include <string>
#include <string_view>

#include "lib/zx/time.h"
#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics::stubs {

using TimezoneProviderBase = SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::intl, PropertyProvider);

class TimezoneProvider : public TimezoneProviderBase {
 public:
  explicit TimezoneProvider(std::string_view default_timezone);

  void SetTimezone(std::string_view timezone);

  // |fuchsia::intl::PropertyProvider|
  void GetProfile(GetProfileCallback callback) override;

 private:
  std::string timezone_;
};

class TimezoneProviderDelaysResponse : public TimezoneProviderBase {
 public:
  TimezoneProviderDelaysResponse(async_dispatcher_t* dispatcher, zx::duration delay,
                                 std::string_view default_timezone);

  // |fuchsia::intl::PropertyProvider|
  void GetProfile(GetProfileCallback callback) override;

 private:
  async_dispatcher_t* dispatcher_;
  zx::duration delay_;
  std::string timezone_;
};

}  // namespace forensics::stubs

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_TIMEZONE_PROVIDER_H_
