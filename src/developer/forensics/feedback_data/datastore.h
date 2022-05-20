// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DATASTORE_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DATASTORE_H_

#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>

#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/feedback_data/attachments/inspect.h"
#include "src/developer/forensics/feedback_data/attachments/kernel_log.h"
#include "src/developer/forensics/feedback_data/attachments/metrics.h"
#include "src/developer/forensics/feedback_data/attachments/system_log.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/inspect_data_budget.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/developer/forensics/utils/previous_boot_file.h"
#include "src/developer/forensics/utils/redact/redactor.h"
#include "src/lib/timekeeper/system_clock.h"

namespace forensics {
namespace feedback_data {

// Holds data useful to attach in feedback reports (crash, user feedback or bug reports).
//
// Data can be annotations or attachments.
//
// Some data are:
// * static and collected at startup, e.g., build version or hardware info.
// * dynamic and collected upon data request, e.g., uptime or logs.
// * collected synchronously, e.g., build version or uptime.
// * collected asynchronously, e.g., hardware info or logs.
// * pushed by other components, we called these "non-platform" to distinguish them from the
//   "platform".
//
// Because of dynamic asynchronous data, the data requests can take some time and return a
// ::fpromise::promise.
class Datastore {
 public:
  Datastore(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
            cobalt::Logger* cobalt, RedactorBase* redactor,
            const AttachmentKeys& attachment_allowlist, InspectDataBudget* inspect_data_budget);

  ::fpromise::promise<Attachments> GetAttachments(zx::duration timeout);

  // Exposed for testing purposes.
  Datastore(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
            const char* limit_data_flag_path);

  const Attachments& GetStaticAttachments() const { return static_attachments_; }

  void DropStaticAttachment(const AttachmentKey& key, Error error);

 private:
  ::fpromise::promise<Attachment> BuildAttachment(const AttachmentKey& key, zx::duration timeout);
  ::fpromise::promise<AttachmentValue> BuildAttachmentValue(const AttachmentKey& key,
                                                            zx::duration timeout);
  fit::Timeout MakeCobaltTimeout(cobalt::TimedOutData data, zx::duration timeout);

  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  cobalt::Logger* cobalt_;
  RedactorBase* redactor_;
  timekeeper::SystemClock clock_;
  AttachmentKeys attachment_allowlist_;

  Attachments static_attachments_;

  InspectDataBudget* inspect_data_budget_;

  AttachmentMetrics attachment_metrics_;
  KernelLog kernel_log_;
  SystemLog system_log_;
  Inspect inspect_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DATASTORE_H_
