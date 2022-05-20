// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENT_MANAGER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENT_MANAGER_H_

#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>

#include "src/developer/forensics/feedback_data/attachments/inspect.h"
#include "src/developer/forensics/feedback_data/attachments/kernel_log.h"
#include "src/developer/forensics/feedback_data/attachments/metrics.h"
#include "src/developer/forensics/feedback_data/attachments/provider.h"
#include "src/developer/forensics/feedback_data/attachments/system_log.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/inspect_data_budget.h"
#include "src/developer/forensics/utils/redact/redactor.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics {
namespace feedback_data {

// Responsible for the storage and collection of attachments
//
// Attachments are either static and collected once at startup or dynamic and collected at runtime
// each time they're needed.
class AttachmentManager {
 public:
  AttachmentManager(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                    timekeeper::Clock* clock, cobalt::Logger* cobalt, RedactorBase* redactor,
                    const AttachmentKeys& allowlist, InspectDataBudget* inspect_data_budget);

  ::fpromise::promise<Attachments> GetAttachments(zx::duration timeout);

  const Attachments& GetStaticAttachments() const { return static_attachments_; }

  void DropStaticAttachment(const AttachmentKey& key, Error error);

 private:
  AttachmentKeys allowlist_;

  Attachments static_attachments_;

  AttachmentMetrics attachment_metrics_;
  KernelLog kernel_log_;
  SystemLog system_log_;
  Inspect inspect_;

  std::map<std::string, AttachmentProvider*> providers_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENT_MANAGER_H_
