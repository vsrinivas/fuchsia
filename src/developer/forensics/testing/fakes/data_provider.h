// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_FAKES_DATA_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_FAKES_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>

namespace forensics {
namespace fakes {

// Fake handler for fuchsia.feedback.DataProvider, returns valid payloads for GetSnpashot() and
// GetScreenshot(). Tests should not have hard expectations on these payloads as they're subject to
// change.
class DataProvider : public fuchsia::feedback::DataProvider {
 public:
  // |fuchsia::feedback::DataProvider|
  void GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                   GetSnapshotCallback callback) override;
  void GetScreenshot(fuchsia::feedback::ImageEncoding encoding,
                     GetScreenshotCallback callback) override;
};

}  // namespace fakes
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_FAKES_DATA_PROVIDER_H_
