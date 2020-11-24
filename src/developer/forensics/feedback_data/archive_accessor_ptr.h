// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ARCHIVE_ACCESSOR_PTR_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ARCHIVE_ACCESSOR_PTR_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fidl/oneshot_ptr.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/lib/fxl/macros.h"

namespace forensics::feedback_data {

// The name of the protocol to use to read Feedback data from the Archive.
constexpr char kArchiveAccessorName[] = "fuchsia.diagnostics.FeedbackArchiveAccessor";

// Wraps around fuchsia::diagnostics::ArchiveAccessorPtr, fuchsia::diagnostics::ReaderPtr and
// fuchsia::diagnostics::BatchIteratorPtr to handle establishing the connection, losing the
// connection, waiting for the callback, enforcing a timeout, etc.
//
// Collect() is expected to be called exactly once.
class ArchiveAccessor {
 public:
  ArchiveAccessor(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                  fuchsia::diagnostics::DataType data_type,
                  fuchsia::diagnostics::StreamMode stream_mode);

  void Collect(std::function<void(fuchsia::diagnostics::FormattedContent)> write_formatted_content);

  ::fit::promise<void, Error> WaitForDone(fit::Timeout timeout);

 private:
  void AppendNextBatch(
      std::function<void(fuchsia::diagnostics::FormattedContent)> write_formatted_content);

  fidl::OneShotPtr<fuchsia::diagnostics::ArchiveAccessor> archive_;
  fuchsia::diagnostics::StreamParameters stream_parameters_;
  fuchsia::diagnostics::BatchIteratorPtr snapshot_iterator_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ArchiveAccessor);
};

}  // namespace forensics::feedback_data

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ARCHIVE_ACCESSOR_PTR_H_
