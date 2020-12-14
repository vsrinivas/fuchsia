// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/inspect_ptr.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>
#include <string>
#include <vector>

#include "src/developer/forensics/feedback_data/archive_accessor_ptr.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/promise.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace forensics {
namespace feedback_data {

::fit::promise<AttachmentValue> CollectInspectData(async_dispatcher_t* dispatcher,
                                                   std::shared_ptr<sys::ServiceDirectory> services,
                                                   fit::Timeout timeout,
                                                   std::optional<size_t> data_budget) {
  std::unique_ptr<ArchiveAccessor> inspect = std::make_unique<ArchiveAccessor>(
      dispatcher, services, fuchsia::diagnostics::DataType::INSPECT,
      fuchsia::diagnostics::StreamMode::SNAPSHOT, data_budget);

  // Accumulated Inspect data. Each element corresponds to one valid Inspect "block" in JSON format.
  // A block would typically be the Inspect data for one component.
  auto inspect_vector = std::make_shared<std::vector<std::string>>();

  // Start collecting data.
  inspect->Collect([inspect_vector](fuchsia::diagnostics::FormattedContent chunk) {
    if (!chunk.is_json()) {
      FX_LOGS(WARNING) << "Invalid JSON Inspect chunk, skipping";
      return;
    }

    std::string json;
    if (!fsl::StringFromVmo(chunk.json(), &json)) {
      FX_LOGS(WARNING) << "Failed to convert Inspect data chunk to string, skipping";
    } else {
      inspect_vector->push_back(json);
    }
  });

  // Wait to receive all data and post process.
  ::fit::promise<AttachmentValue> inspect_data =
      inspect->WaitForDone(std::move(timeout))
          .then([inspect_vector](
                    ::fit::result<void, Error>& result) -> ::fit::result<AttachmentValue> {
            if (inspect_vector->empty()) {
              FX_LOGS(WARNING) << "Empty Inspect data";
              AttachmentValue value = (result.is_ok()) ? AttachmentValue(Error::kMissingValue)
                                                       : AttachmentValue(result.error());
              return ::fit::ok(std::move(value));
            }

            std::string joined_data = "[\n";
            joined_data += fxl::JoinStrings(*inspect_vector.get(), ",\n");
            joined_data += "\n]";

            AttachmentValue value = (result.is_ok())
                                        ? AttachmentValue(std::move(joined_data))
                                        : AttachmentValue(std::move(joined_data), result.error());

            return ::fit::ok(std::move(value));
          });

  return fit::ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(inspect_data),
                                              /*args=*/std::move(inspect));
}

}  // namespace feedback_data
}  // namespace forensics
