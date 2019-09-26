// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/bugreport/tests/stub_feedback_data_provider.h"

#include <lib/fit/result.h>

namespace feedback {

void StubFeedbackDataProvider::GetData(GetDataCallback callback) {
  callback(fit::ok(
      std::move(fuchsia::feedback::Data().set_attachment_bundle(std::move(attachment_bundle_)))));
}

}  // namespace feedback
