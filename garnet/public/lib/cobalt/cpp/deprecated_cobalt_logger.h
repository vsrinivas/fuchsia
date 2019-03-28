// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_PUBLIC_LIB_COBALT_CPP_COBALT_LOGGER_H_
#define GARNET_PUBLIC_LIB_COBALT_CPP_COBALT_LOGGER_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/zx/time.h>

#include "src/lib/cobalt/cpp/cobalt_logger.h"

namespace cobalt {

// Returns a CobaltLogger initialized with the provided parameters.
//
// |dispatcher| A pointer to an async_dispatcher_t to be used for all
// asynchronous operations.
//
// |context| A pointer to the StartupContext that provides access to the
// environment of the component using this CobaltLogger.
//
// |config_path| The path to the configuration file for the Cobalt project
// associated with the new Logger. This is a binary file containing the compiled
// definitions of the metrics and reports defined for the project. Usually this
// file is generated via the |cobalt_config| target in your BUILD file and
// included in your package via a |resources| clause in your |package|
// definition.
//
// |release_stage| Optional specification of the current release stage of the
// project associated with the new Logger. This determines which of the defined
// metrics are permitted to be collected. The default value of GA (Generally
// Available) permits only metrics tagged as GA.
std::unique_ptr<CobaltLogger> DeprecatedNewCobaltLogger(
    async_dispatcher_t* dispatcher, component::StartupContext* context,
    const std::string& config_path,
    fuchsia::cobalt::ReleaseStage release_stage =
        fuchsia::cobalt::ReleaseStage::GA);

// Returns a CobaltLogger initialized with the provided parameters.
//
// |dispatcher| A pointer to an async_dispatcher_t to be used for all
// asynchronous operations.
//
// |context| A pointer to the StartupContext that provides access to the
// environment of the component using this CobaltLogger.
//
// |profile| The ProjectProfile struct that contains the configuration for this
// CobaltLogger.
std::unique_ptr<CobaltLogger> DeprecatedNewCobaltLogger(
    async_dispatcher_t* dispatcher, component::StartupContext* context,
    fuchsia::cobalt::ProjectProfile profile);

}  // namespace cobalt

#endif  // GARNET_PUBLIC_LIB_COBALT_CPP_COBALT_LOGGER_H_
