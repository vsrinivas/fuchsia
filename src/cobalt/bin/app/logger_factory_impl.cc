// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/logger_factory_impl.h"

#include "src/cobalt/bin/app/utils.h"
#include "src/lib/fsl/vmo/strings.h"

namespace cobalt {

using cobalt::TimerManager;
using config::ProjectConfigs;
using fuchsia::cobalt::Status;

constexpr uint32_t kFuchsiaCustomerId = 1;

LoggerFactoryImpl::LoggerFactoryImpl(TimerManager* timer_manager,
                                     CobaltServiceInterface* cobalt_service)
    : timer_manager_(timer_manager), cobalt_service_(cobalt_service) {}

template <typename LoggerInterface, typename Callback>
void LoggerFactoryImpl::CreateAndBindLoggerFromProjectId(
    uint32_t project_id, fidl::InterfaceRequest<LoggerInterface> request, Callback callback,
    fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>* binding_set) {
  auto logger = cobalt_service_->NewLogger(kFuchsiaCustomerId, project_id);
  if (!logger) {
    FX_LOGS(ERROR) << "The CobaltRegistry bundled with this release does not "
                      "include a Fuchsia project with ID "
                   << project_id;
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  binding_set->AddBinding(std::make_unique<LoggerImpl>(std::move(logger), timer_manager_),
                          std::move(request));
  callback(Status::OK);
}

void LoggerFactoryImpl::CreateLoggerFromProjectId(
    uint32_t project_id, fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
    CreateLoggerFromProjectIdCallback callback) {
  CreateAndBindLoggerFromProjectId(project_id, std::move(request), std::move(callback),
                                   &logger_bindings_);
}

void LoggerFactoryImpl::CreateLoggerSimpleFromProjectId(
    uint32_t project_id, fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
    CreateLoggerSimpleFromProjectIdCallback callback) {
  CreateAndBindLoggerFromProjectId(project_id, std::move(request), std::move(callback),
                                   &logger_simple_bindings_);
}

}  // namespace cobalt
