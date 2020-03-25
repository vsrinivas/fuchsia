// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_FAKES_FAKE_DEVICE_ID_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_FAKES_FAKE_DEVICE_ID_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <optional>
#include <string>

namespace feedback {

// Fake handler for fuchsia.feedback.DeviceIdProvider.
class FakeDeviceIdProvider : public fuchsia::feedback::DeviceIdProvider {
 public:
  // |fuchsia.feedback.DeviceIdProvider|
  void GetId(GetIdCallback callback) override;

 private:
  std::unique_ptr<std::optional<std::string>> device_id_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_FAKES_FAKE_DEVICE_ID_PROVIDER_H_
