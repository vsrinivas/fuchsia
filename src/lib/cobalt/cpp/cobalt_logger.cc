// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/cobalt/cpp/cobalt_logger.h"

#include <fuchsia/cobalt/cpp/fidl.h>

#include "src/lib/cobalt/cpp/cobalt_logger_impl.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/syslog/cpp/logger.h"

using fuchsia::cobalt::ProjectProfile;

namespace cobalt {

std::unique_ptr<CobaltLogger> NewCobaltLogger(async_dispatcher_t* dispatcher,
                                              std::shared_ptr<sys::ServiceDirectory> services,
                                              const std::string& config_path,
                                              fuchsia::cobalt::ReleaseStage release_stage) {
  fsl::SizedVmo config_vmo;
  if (!fsl::VmoFromFilename(config_path, &config_vmo)) {
    FX_LOGST(ERROR, "cobalt_lib") << "Could not find config file at " << config_path;
    return nullptr;
  }

  ProjectProfile profile;
  profile.config = std::move(config_vmo).ToTransport();
  profile.release_stage = release_stage;
  return NewCobaltLogger(dispatcher, services, std::move(profile));
}

std::unique_ptr<CobaltLogger> NewCobaltLogger(async_dispatcher_t* dispatcher,
                                              std::shared_ptr<sys::ServiceDirectory> services,
                                              ProjectProfile profile) {
  return std::make_unique<CobaltLoggerImpl>(dispatcher, services, std::move(profile));
}

std::unique_ptr<CobaltLogger> NewCobaltLoggerFromProjectId(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    uint32_t project_id) {
  return std::make_unique<CobaltLoggerImpl>(dispatcher, services, project_id);
}

}  // namespace cobalt
