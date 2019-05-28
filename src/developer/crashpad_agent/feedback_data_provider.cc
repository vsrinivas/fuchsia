// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/feedback_data_provider.h"

#include <lib/syslog/cpp/logger.h>
#include <zircon/types.h>

namespace fuchsia {
namespace crash {

FeedbackDataProvider::FeedbackDataProvider(
    std::shared_ptr<::sys::ServiceDirectory> services)
    : services_(services) {}

fit::promise<fuchsia::feedback::Data> FeedbackDataProvider::GetData() {
  data_provider_ = services_->Connect<fuchsia::feedback::DataProvider>();
  data_provider_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status)
        << "Lost connection to fuchsia.feedback.DataProvider";
    if (done_.completer) {
      done_.completer.complete_error();
    }
  });
  data_provider_->GetData(
      [this](fuchsia::feedback::DataProvider_GetData_Result out_result) {
        if (out_result.is_err()) {
          FX_PLOGS(WARNING, out_result.err())
              << "Failed to fetch feedback data";
          if (done_.completer) {
            done_.completer.complete_error();
          }
          return;
        }

        if (done_.completer) {
          done_.completer.complete_ok(std::move(out_result.response().data));
        }
      });

  return done_.consumer.promise_or(fit::error());
}

}  // namespace crash
}  // namespace fuchsia
