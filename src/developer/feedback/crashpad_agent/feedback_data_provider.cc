// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/feedback_data_provider.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/result.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

using fuchsia::feedback::Data;

}  // namespace

FeedbackDataProvider::FeedbackDataProvider(async_dispatcher_t* dispatcher,
                                           std::shared_ptr<sys::ServiceDirectory> services)
    : dispatcher_(dispatcher), services_(services), pending_get_data_(dispatcher_) {}

fit::promise<Data> FeedbackDataProvider::GetData(zx::duration timeout) {
  if (!data_provider_) {
    ConnectToDataProvider();
  }

  const uint64_t id = pending_get_data_.NewBridgeForTask("Feedback data collection");

  data_provider_->GetData([id, this](fit::result<Data, zx_status_t> result) {
    if (pending_get_data_.IsAlreadyDone(id)) {
      return;
    }

    if (result.is_error()) {
      FX_PLOGS(WARNING, result.error()) << "Failed to fetch feedback data";
      pending_get_data_.CompleteError(id);
    } else {
      pending_get_data_.CompleteOk(id, result.take_value());
    }
  });

  return pending_get_data_.WaitForDone(id, timeout).then([id, this](fit::result<Data>& result) {
    // We need to move the result before erasing the bridge because |result| is passed as a
    // reference.
    fit::result<Data> data = std::move(result);

    pending_get_data_.Delete(id);

    // Close the connection if we were the last pending call to GetData().
    if (pending_get_data_.IsEmpty()) {
      data_provider_.Unbind();
    }

    return data;
  });
}

void FeedbackDataProvider::ConnectToDataProvider() {
  if (data_provider_) {
    return;
  }

  data_provider_ = services_->Connect<fuchsia::feedback::DataProvider>();

  data_provider_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.feedback.DataProvider";

    pending_get_data_.CompleteAllError();
  });
}

}  // namespace feedback
