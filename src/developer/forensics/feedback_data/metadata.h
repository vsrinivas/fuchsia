// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_METADATA_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_METADATA_H_

#include <lib/async/dispatcher.h>
#include <lib/fpromise/result.h>

#include <optional>
#include <string>

#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/feedback/attachments/types.h"
#include "src/developer/forensics/utils/redact/redactor.h"
#include "src/developer/forensics/utils/utc_clock_ready_watcher.h"
#include "src/developer/forensics/utils/utc_time_provider.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics {
namespace feedback_data {

// Constructs metadata describing the rest of the content of the snapshot archive.
class Metadata {
 public:
  Metadata(async_dispatcher_t* dispatcher, timekeeper::Clock* clock, RedactorBase* redactor,
           bool is_first_instance, const std::set<std::string>& annotation_allowlist,
           const feedback::AttachmentKeys& attachment_allowlist);

  // Return a JSON metadata string.
  //
  // |missing_non_platform_annotations| indicates whether some non-platform annotations are
  // missing, i.e. whether clients tried to insert more non-platform annotations than the maximum
  // number of non-platform annotations the AttachmentManager can hold.
  std::string MakeMetadata(const feedback::Annotations& annotations,
                           const feedback::Attachments& attachments,
                           const std::string& snapshot_uuid, bool missing_non_platform_annotations);

  static constexpr const char* kVersion = "1";

 private:
  std::string log_redaction_canary_;
  std::set<std::string> annotation_allowlist_;
  feedback::AttachmentKeys attachment_allowlist_;

  UtcClockReadyWatcher utc_clock_ready_watcher_;
  UtcTimeProvider utc_provider_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_METADATA_H_
