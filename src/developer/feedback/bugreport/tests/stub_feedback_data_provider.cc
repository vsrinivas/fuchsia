// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/bugreport/tests/stub_feedback_data_provider.h"

namespace fuchsia {
namespace bugreport {

void StubFeedbackDataProvider::GetData(GetDataCallback callback) {
  fuchsia::feedback::DataProvider_GetData_Result result;
  fuchsia::feedback::DataProvider_GetData_Response response;
  response.data.set_attachment_bundle(std::move(attachment_bundle_));
  result.set_response(std::move(response));
  callback(std::move(result));
}

}  // namespace bugreport
}  // namespace fuchsia
