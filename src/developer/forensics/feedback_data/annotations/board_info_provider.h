// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_BOARD_INFO_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_BOARD_INFO_PROVIDER_H_

#include <fuchsia/hwinfo/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/time.h>

#include "src/developer/forensics/feedback_data/annotations/annotation_provider.h"
#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/fidl/caching_ptr.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/lib/fxl/macros.h"

namespace forensics {
namespace feedback_data {

// Get the requested parts of fuchsia.hwinfo.BoardInfo as annotations.
class BoardInfoProvider : public AnnotationProvider {
 public:
  // fuchsia.hwinfo.Board is expected to be in |services|.
  BoardInfoProvider(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                    cobalt::Logger* cobalt);

  ::fpromise::promise<Annotations> GetAnnotations(zx::duration timeout,
                                                  const AnnotationKeys& allowlist) override;

 private:
  void GetInfo();

  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  cobalt::Logger* cobalt_;

  fidl::CachingPtr<fuchsia::hwinfo::Board, std::map<AnnotationKey, std::string>> board_ptr_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_BOARD_INFO_PROVIDER_H_
