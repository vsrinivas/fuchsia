// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/logger_factory_impl.h"

#include "src/cobalt/bin/app/utils.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/strings.h"

namespace cobalt {

using cobalt::TimerManager;
using cobalt::logger::ProjectContextFactory;
using config::ProjectConfigs;
using fuchsia::cobalt::ProjectProfile;
using fuchsia::cobalt::Status;

constexpr uint32_t kFuchsiaCustomerId = 1;

namespace {

bool ExtractCobaltRegistryBytes(ProjectProfile profile, std::string* metrics_registry_bytes_out) {
  CHECK(metrics_registry_bytes_out);
  fsl::SizedVmo registry_vmo;
  bool success = fsl::SizedVmo::FromTransport(std::move(profile.config), &registry_vmo);
  if (!success) {
    FX_LOGS(ERROR) << "Transport buffer is invalid";
    return false;
  }

  success = fsl::StringFromVmo(registry_vmo, metrics_registry_bytes_out);
  if (!success) {
    FX_LOGS(ERROR) << "Could not read Metrics Registry from VMO";
    return false;
  }

  return true;
}

}  // namespace

LoggerFactoryImpl::LoggerFactoryImpl(
    std::shared_ptr<cobalt::logger::ProjectContextFactory> global_project_context_factory,
    TimerManager* timer_manager, CobaltService* cobalt_service)
    : global_project_context_factory_(std::move(global_project_context_factory)),
      timer_manager_(timer_manager),
      cobalt_service_(cobalt_service) {}

template <typename LoggerInterface>
void LoggerFactoryImpl::BindNewLogger(
    std::unique_ptr<logger::ProjectContext> project_context,
    fidl::InterfaceRequest<LoggerInterface> request,
    fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>* binding_set) {
  binding_set->AddBinding(
      std::make_unique<LoggerImpl>(cobalt_service_->NewLogger(std::move(project_context)),
                                   timer_manager_),
      std::move(request));
}

template <typename LoggerInterface, typename Callback>
void LoggerFactoryImpl::CreateAndBindLogger(
    fuchsia::cobalt::ProjectProfile profile, fidl::InterfaceRequest<LoggerInterface> request,
    Callback callback,
    fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>* binding_set) {
  std::string cobalt_registry_bytes;
  if (!ExtractCobaltRegistryBytes(std::move(profile), &cobalt_registry_bytes)) {
    FX_LOGS(ERROR) << "Unable to extract a CobaltRegistry from the provided "
                      "ProjectProfile.";
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  std::shared_ptr<ProjectContextFactory> factory(new ProjectContextFactory(cobalt_registry_bytes));
  if (!factory->is_valid()) {
    FX_LOGS(ERROR) << "Unable to extract a valid CobaltRegistry from the "
                      "provided ProjectProfile.";
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  if (factory->is_single_project()) {
    BindNewLogger(factory->TakeSingleProjectContext(), std::move(request), binding_set);
    callback(Status::OK);
    return;
  } else {
    FX_LOGS(ERROR) << "The CobaltRegistry in the provided ProjectProfile was "
                      "invalid because it contained multiple projects.";
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
}

template <typename LoggerInterface, typename Callback>
void LoggerFactoryImpl::CreateAndBindLoggerFromProjectId(
    uint32_t project_id, fidl::InterfaceRequest<LoggerInterface> request, Callback callback,
    fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>* binding_set) {
  auto project_context =
      global_project_context_factory_->NewProjectContext(kFuchsiaCustomerId, project_id);
  if (!project_context) {
    FX_LOGS(ERROR) << "The CobaltRegistry bundled with this release does not "
                      "include a Fuchsia project with ID "
                   << project_id;
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  BindNewLogger(std::move(project_context), std::move(request), binding_set);
  callback(Status::OK);
}

void LoggerFactoryImpl::CreateLogger(ProjectProfile profile,
                                     fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
                                     CreateLoggerCallback callback) {
  CreateAndBindLogger(std::move(profile), std::move(request), std::move(callback),
                      &logger_bindings_);
}

void LoggerFactoryImpl::CreateLoggerSimple(
    ProjectProfile profile, fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
    CreateLoggerSimpleCallback callback) {
  CreateAndBindLogger(std::move(profile), std::move(request), std::move(callback),
                      &logger_simple_bindings_);
}

void LoggerFactoryImpl::CreateLoggerFromProjectId(
    uint32_t project_id, fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
    CreateLoggerFromProjectIdCallback callback) {
  CreateAndBindLoggerFromProjectId(std::move(project_id), std::move(request), std::move(callback),
                                   &logger_bindings_);
}

void LoggerFactoryImpl::CreateLoggerSimpleFromProjectId(
    uint32_t project_id, fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
    CreateLoggerSimpleFromProjectIdCallback callback) {
  CreateAndBindLoggerFromProjectId(std::move(project_id), std::move(request), std::move(callback),
                                   &logger_simple_bindings_);
}

}  // namespace cobalt
