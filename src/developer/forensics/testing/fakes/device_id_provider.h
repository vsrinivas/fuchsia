// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_FAKES_DEVICE_ID_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_FAKES_DEVICE_ID_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <optional>
#include <string>

namespace forensics {
namespace fakes {

// Fake handler for fuchsia.feedback.DeviceIdProvider.
class DeviceIdProvider : public fuchsia::feedback::DeviceIdProvider {
 public:
  DeviceIdProvider();

  // |fuchsia.feedback.DeviceIdProvider|
  void GetId(GetIdCallback callback) override;

 private:
  std::string device_id_;
};

}  // namespace fakes
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_FAKES_DEVICE_ID_PROVIDER_H_
