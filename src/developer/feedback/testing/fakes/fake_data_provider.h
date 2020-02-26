// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_FAKES_FAKE_DATA_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_FAKES_FAKE_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>

namespace feedback {

// Fake handler for fuchsia.feedback.DataProvider, returns valid payloads for GetData() and
// GetScreenshot(). Tests should not have hard expectations on these payloads as they're subject to
// change.
class FakeDataProvider : public fuchsia::feedback::DataProvider {
 public:
  // |fuchsia::feedback::DataProvider|
  void GetData(GetDataCallback callback) override;
  void GetScreenshot(fuchsia::feedback::ImageEncoding encoding,
                     GetScreenshotCallback callback) override;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_FAKES_FAKE_DATA_PROVIDER_H_
