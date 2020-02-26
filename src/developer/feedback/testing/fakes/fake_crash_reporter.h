// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_FAKES_FAKE_CRASH_REPORTER_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_FAKES_FAKE_CRASH_REPORTER_H_

#include <fuchsia/feedback/cpp/fidl.h>

namespace feedback {

// Fake handler for fuchsia.feedback.CrashReporter, returns an error if the filed crash report
// doesn't contain a program name. Otherwise, an ok reponse is returned.
class FakeCrashReporter : public fuchsia::feedback::CrashReporter {
 public:
  // |fuchsia::feedback::CrashReporter|
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_FAKES_FAKE_CRASH_REPORTER_H_
