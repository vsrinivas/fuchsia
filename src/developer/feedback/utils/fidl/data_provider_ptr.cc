// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/fidl/data_provider_ptr.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/developer/feedback/utils/errors.h"

namespace forensics {
namespace fidl {
namespace {

using fuchsia::feedback::Bugreport;

}  // namespace

DataProviderPtr::DataProviderPtr(async_dispatcher_t* dispatcher,
                                 std::shared_ptr<sys::ServiceDirectory> services)
    : services_(services), pending_calls_(dispatcher) {}

::fit::promise<Bugreport, Error> DataProviderPtr::GetBugreport(const zx::duration timeout) {
  if (!connection_) {
    Connect();
  }

  const uint64_t id = pending_calls_.NewBridgeForTask("Feedback data collection");

  connection_->GetBugreport(
      // We give 15s for the packaging of the bugreport and the round-trip between the client and
      // the server and the rest is given to each data collection.
      std::move(fuchsia::feedback::GetBugreportParameters().set_collection_timeout_per_data(
          (timeout - zx::sec(15) /* cost of making the call and packaging the bugreport */).get())),
      [id, this](Bugreport bugreport) {
        if (pending_calls_.IsAlreadyDone(id)) {
          return;
        }

        pending_calls_.CompleteOk(id, std::move(bugreport));
      });

  return pending_calls_.WaitForDone(id, fit::Timeout(timeout))
      .then([id, this](::fit::result<Bugreport, Error>& result) {
        // We need to move the result before erasing the bridge because |result| is passed as a
        // reference.
        ::fit::result<Bugreport, Error> bugreport = std::move(result);

        pending_calls_.Delete(id);

        // Close the connection if we were the last pending call to GetBugreport().
        if (pending_calls_.IsEmpty()) {
          connection_.Unbind();
        }

        return bugreport;
      });
}

void DataProviderPtr::Connect() {
  if (connection_) {
    return;
  }

  connection_ = services_->Connect<fuchsia::feedback::DataProvider>();

  connection_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.feedback.DataProvider";

    pending_calls_.CompleteAllError(Error::kConnectionError);
  });
}

}  // namespace fidl
}  // namespace forensics
