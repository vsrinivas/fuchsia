// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_data/annotations/annotation_provider_factory.h"

#include <memory>
#include <vector>

#include "src/developer/feedback/feedback_data/annotations/board_info_provider.h"
#include "src/developer/feedback/feedback_data/annotations/channel_provider.h"
#include "src/developer/feedback/feedback_data/annotations/product_info_provider.h"
#include "src/developer/feedback/feedback_data/annotations/time_provider.h"
#include "src/developer/feedback/feedback_data/constants.h"
#include "src/developer/feedback/utils/cobalt.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/timekeeper/system_clock.h"

namespace feedback {

std::vector<std::unique_ptr<AnnotationProvider>> GetProviders(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    const zx::duration timeout, Cobalt* cobalt) {
  std::vector<std::unique_ptr<AnnotationProvider>> providers;

  providers.push_back(std::make_unique<ChannelProvider>(dispatcher, services, timeout, cobalt));
  providers.push_back(std::make_unique<BoardInfoProvider>(dispatcher, services, timeout, cobalt));
  providers.push_back(std::make_unique<ProductInfoProvider>(dispatcher, services, timeout, cobalt));
  providers.push_back(std::make_unique<TimeProvider>(std::make_unique<timekeeper::SystemClock>()));

  // We don't warn on annotations present in the allowlist that were not collected as there could
  // be static annotations.

  return providers;
}

}  // namespace feedback
