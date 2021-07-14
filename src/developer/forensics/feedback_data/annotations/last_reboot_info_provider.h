// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_LAST_REBOOT_INFO_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_LAST_REBOOT_INFO_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include "src/developer/forensics/feedback_data/annotations/annotation_provider.h"
#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/fidl/caching_ptr.h"

namespace forensics {
namespace feedback_data {

// Get the requested parts of fuchsia.feedback.LastReboot as annotations.
class LastRebootInfoProvider : public AnnotationProvider {
 public:
  // fuchsia.feedback.LastRebootInfoProvider is expected to be in |services|.
  LastRebootInfoProvider(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services, cobalt::Logger* cobalt);

  ::fpromise::promise<Annotations> GetAnnotations(zx::duration timeout,
                                                  const AnnotationKeys& allowlist) override;

 private:
  void GetLastReboot();

  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  cobalt::Logger* cobalt_;

  fidl::CachingPtr<fuchsia::feedback::LastRebootInfoProvider, std::map<AnnotationKey, std::string>>
      last_reboot_info_ptr_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_LAST_REBOOT_INFO_PROVIDER_H_
