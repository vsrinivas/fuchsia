// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_LAST_REBOOT_INFO_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_LAST_REBOOT_INFO_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl_test_base.h>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics {
namespace stubs {

using LastRebootInfoProviderBase = SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::feedback,
                                                                   LastRebootInfoProvider);

class LastRebootInfoProvider : public LastRebootInfoProviderBase {
 public:
  LastRebootInfoProvider(fuchsia::feedback::LastReboot&& last_reboot)
      : last_reboot_(std::move(last_reboot)) {}

  // |fuchsia::feedback::LastRebootInfoProvider|
  void Get(GetCallback callback) override;

 private:
  fuchsia::feedback::LastReboot last_reboot_;
  bool has_been_called_ = false;
};

class LastRebootInfoProviderNeverReturns : public LastRebootInfoProviderBase {
 public:
  // |fuchsia::feedback::LastRebootInfoProvider|
  STUB_METHOD_DOES_NOT_RETURN(Get, GetCallback);
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_LAST_REBOOT_INFO_PROVIDER_H_
