// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENT_PROVIDERS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENT_PROVIDERS_H_

#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <set>
#include <string>

#include "src/developer/forensics/feedback_data/attachments/attachment_manager.h"
#include "src/developer/forensics/feedback_data/attachments/inspect.h"
#include "src/developer/forensics/feedback_data/attachments/kernel_log.h"
#include "src/developer/forensics/feedback_data/attachments/system_log.h"
#include "src/developer/forensics/feedback_data/inspect_data_budget.h"
#include "src/developer/forensics/utils/redact/redactor.h"
#include "src/lib/backoff/backoff.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics::feedback_data {

// Wraps the annotations providers Feedback uses and the component's AttachmentManager.
class AttachmentProviders {
 public:
  AttachmentProviders(async_dispatcher_t* dispatcher,
                      std::shared_ptr<sys::ServiceDirectory> services, timekeeper::Clock* clock,
                      RedactorBase* redactor, InspectDataBudget* inspect_data_budget,
                      std::set<std::string> allowlist, Attachments static_attachments);

  AttachmentManager* GetAttachmentManager() { return &attachment_manager_; }

  static std::unique_ptr<backoff::Backoff> AttachmentProviderBackoff();

 private:
  KernelLog kernel_log_;
  SystemLog system_log_;
  Inspect inspect_;

  AttachmentManager attachment_manager_;
};

}  // namespace forensics::feedback_data

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENT_PROVIDERS_H_
