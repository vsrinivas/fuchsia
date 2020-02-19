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
    : dispatcher_(dispatcher), services_(services) {}

fit::promise<Data> FeedbackDataProvider::GetData(zx::duration timeout) {
  if (!data_provider_) {
    ConnectToDataProvider();
  }

  const uint64_t id = next_get_data_id_++;
  auto done = pending_get_data_[id].GetWeakPtr();

  // Post a task on the loop that will complete the bridge to get data with an error if data
  // collection doesn't complete before |timeout| elapses.
  if (const zx_status_t status = async::PostDelayedTask(
          dispatcher_,
          [=] {
            if (!done || !done->completer) {
              return;
            }

            FX_LOGS(ERROR) << "Feedback data collection timed out";
            done->completer.complete_error();
          },
          timeout);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to post delayed task";
    FX_LOGS(ERROR) << "Skipping Feedback data collection as it is not safe without a timeout";
    return fit::make_result_promise<Data>(fit::error());
  }

  data_provider_->GetData([=](fit::result<Data, zx_status_t> result) {
    if (!done || !done->completer) {
      return;
    }

    if (result.is_error()) {
      FX_PLOGS(WARNING, result.error()) << "Failed to fetch feedback data";
      done->completer.complete_error();
    } else {
      done->completer.complete_ok(result.take_value());
    }
  });

  return done->consumer.promise_or(fit::error()).then([=](fit::result<Data>& result) {
    // We need to move the result before erasing the bridge because |result| is passed as a
    // reference.
    fit::result<Data> data = std::move(result);

    pending_get_data_.erase(id);

    // Close the connection if we were the last pending call to GetData().
    if (pending_get_data_.size() == 0) {
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

    // Return an error on all pending GetData().
    for (auto& [_, bridge] : pending_get_data_) {
      auto done = bridge.GetWeakPtr();
      if (done && done->completer) {
        done->completer.complete_error();
      }
    }
  });
}

}  // namespace feedback
