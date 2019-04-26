// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/cobalt/cpp/deprecated_cobalt_logger_impl.h"

namespace cobalt {

using fuchsia::cobalt::LoggerFactory;
using fuchsia::cobalt::ProjectProfile;
using fuchsia::cobalt::ReleaseStage;

DeprecatedCobaltLoggerImpl::DeprecatedCobaltLoggerImpl(
    async_dispatcher_t* dispatcher, component::StartupContext* context,
    ProjectProfile profile)
    : BaseCobaltLoggerImpl(dispatcher, "", ReleaseStage::GA,
                           std::move(profile)),
      component_context_(context) {
  FXL_CHECK(component_context_);
  ConnectToCobaltApplication();
}

fidl::InterfacePtr<LoggerFactory>
DeprecatedCobaltLoggerImpl::ConnectToLoggerFactory() {
  return component_context_->ConnectToEnvironmentService<LoggerFactory>();
}

}  // namespace cobalt
