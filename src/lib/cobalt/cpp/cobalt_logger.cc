// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/cobalt/cpp/cobalt_logger.h"

#include <fuchsia/cobalt/cpp/fidl.h>

#include "src/lib/cobalt/cpp/cobalt_logger_impl.h"
#include "src/lib/syslog/cpp/logger.h"

namespace cobalt {

std::unique_ptr<CobaltLogger> NewCobaltLoggerFromProjectId(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    uint32_t project_id) {
  return std::make_unique<CobaltLoggerImpl>(dispatcher, services, project_id);
}

}  // namespace cobalt
