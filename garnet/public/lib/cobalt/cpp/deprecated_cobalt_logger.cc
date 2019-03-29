// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/cobalt/cpp/deprecated_cobalt_logger_impl.h"

#include "garnet/public/lib/cobalt/cpp/deprecated_cobalt_logger.h"

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/fsl/vmo/file.h>
#include <src/lib/fxl/logging.h>

using fuchsia::cobalt::ProjectProfile;

namespace cobalt {

std::unique_ptr<CobaltLogger> DeprecatedNewCobaltLogger(
    async_dispatcher_t* dispatcher, component::StartupContext* context,
    const std::string& config_path,
    fuchsia::cobalt::ReleaseStage release_stage) {
  fsl::SizedVmo config_vmo;
  if (!fsl::VmoFromFilename(config_path, &config_vmo)) {
    FXL_LOG(ERROR) << "Could not find config file at " << config_path;
    return nullptr;
  }

  ProjectProfile profile;
  profile.config = std::move(config_vmo).ToTransport();
  profile.release_stage = release_stage;
  return std::make_unique<DeprecatedCobaltLoggerImpl>(dispatcher, context,
                                                      std::move(profile));
}

std::unique_ptr<CobaltLogger> DeprecatedNewCobaltLogger(
    async_dispatcher_t* dispatcher, component::StartupContext* context,
    ProjectProfile profile) {
  return std::make_unique<DeprecatedCobaltLoggerImpl>(dispatcher, context,
                                                      std::move(profile));
}

}  // namespace cobalt
