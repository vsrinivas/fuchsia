// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/fidl/data_provider_ptr.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/developer/feedback/utils/errors.h"
#include "src/lib/fxl/logging.h"

namespace feedback {
namespace fidl {
namespace {

using fuchsia::feedback::Data;

}  // namespace

DataProviderPtr::DataProviderPtr(async_dispatcher_t* dispatcher,
                                 std::shared_ptr<sys::ServiceDirectory> services)
    : services_(services), pending_calls_(dispatcher) {}

::fit::promise<Data> DataProviderPtr::GetData(const zx::duration timeout) {
  if (!connection_) {
    Connect();
  }

  const uint64_t id = pending_calls_.NewBridgeForTask("Feedback data collection");

  connection_->GetData([id, this](::fit::result<Data, zx_status_t> result) {
    if (pending_calls_.IsAlreadyDone(id)) {
      return;
    }

    if (result.is_error()) {
      FX_PLOGS(WARNING, result.error()) << "Failed to fetch feedback data";
      pending_calls_.CompleteError(id, Error::kDefault);
    } else {
      pending_calls_.CompleteOk(id, result.take_value());
    }
  });

  return pending_calls_.WaitForDone(id, fit::Timeout(timeout))
      .then([id, this](::fit::result<Data, Error>& result) -> ::fit::result<Data> {
        // We need to move the result before erasing the bridge because |result| is passed as a
        // reference.
        ::fit::result<Data, Error> data = std::move(result);

        pending_calls_.Delete(id);

        // Close the connection if we were the last pending call to GetData().
        if (pending_calls_.IsEmpty()) {
          connection_.Unbind();
        }

        if (data.is_error()) {
          return ::fit::error();
        } else {
          return ::fit::ok(data.take_value());
        }
      });
}

void DataProviderPtr::Connect() {
  if (connection_) {
    return;
  }

  connection_ = services_->Connect<fuchsia::feedback::DataProvider>();

  connection_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.feedback.DataProvider";

    pending_calls_.CompleteAllError(Error::kDefault);
  });
}

}  // namespace fidl
}  // namespace feedback
