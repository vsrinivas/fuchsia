// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_FAKES_LAST_REBOOT_INFO_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_FAKES_LAST_REBOOT_INFO_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>

namespace forensics {
namespace fakes {

// Fake handler for fuchsia.feedback.LastReootInfoProvider. Returns that the last reboot was
// graceful and with an uptime.
class LastRebootInfoProvider : public fuchsia::feedback::LastRebootInfoProvider {
 public:
  // |fuchsia::feedback::LastRebootInfoProvider|
  void Get(GetCallback callback) override;
};

}  // namespace fakes
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_FAKES_LAST_REBOOT_INFO_PROVIDER_H_
