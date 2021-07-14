// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_CHANNEL_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_CHANNEL_PROVIDER_H_

#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>

#include "src/developer/forensics/feedback_data/annotations/annotation_provider.h"
#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/lib/fxl/macros.h"

namespace forensics {
namespace feedback_data {

class ChannelProvider : public AnnotationProvider {
 public:
  // fuchsia.update.channelcontrol.ChannelControl is expected to be in |services|.
  ChannelProvider(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                  cobalt::Logger* cobalt);

  ::fpromise::promise<Annotations> GetAnnotations(zx::duration timeout,
                                                  const AnnotationKeys& allowlist) override;

 private:
  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  cobalt::Logger* cobalt_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ChannelProvider);
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_CHANNEL_PROVIDER_H_
