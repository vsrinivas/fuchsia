// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/logger_factory_impl.h"

#include "lib/fsl/vmo/file.h"
#include "lib/fsl/vmo/strings.h"
#include "src/cobalt/bin/app/utils.h"

namespace cobalt {

using cobalt::TimerManager;
using cobalt::logger::ProjectContextFactory;
using config::ProjectConfigs;
using fuchsia::cobalt::ProjectProfile;
using fuchsia::cobalt::Status;

constexpr char kFuchsiaCustomerName[] = "fuchsia";

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

ReleaseStage ToReleaseStageProto(fuchsia::cobalt::ReleaseStage stage) {
  switch (stage) {
    case fuchsia::cobalt::ReleaseStage::GA:
      return ReleaseStage::GA;
    case fuchsia::cobalt::ReleaseStage::DOGFOOD:
      return ReleaseStage::DOGFOOD;
    case fuchsia::cobalt::ReleaseStage::FISHFOOD:
      return ReleaseStage::FISHFOOD;
    case fuchsia::cobalt::ReleaseStage::DEBUG:
      return ReleaseStage::DEBUG;
    default:
      FX_LOGS(ERROR) << "Unknown ReleaseStage provided. Defaulting to DEBUG.";
      return ReleaseStage::DEBUG;
  }
}
}  // namespace

LoggerFactoryImpl::LoggerFactoryImpl(
    std::shared_ptr<cobalt::logger::ProjectContextFactory> global_project_context_factory,
    encoder::ClientSecret client_secret, TimerManager* timer_manager,
    logger::Encoder* logger_encoder, logger::ObservationWriter* observation_writer,
    logger::EventAggregator* event_aggregator, logger::Logger* internal_logger,
    encoder::SystemDataInterface* system_data)
    : client_secret_(std::move(client_secret)),
      global_project_context_factory_(std::move(global_project_context_factory)),
      timer_manager_(timer_manager),
      logger_encoder_(logger_encoder),
      observation_writer_(observation_writer),
      event_aggregator_(event_aggregator),
      internal_logger_(internal_logger),
      system_data_(system_data) {}

template <typename LoggerInterface>
void LoggerFactoryImpl::BindNewLogger(
    std::unique_ptr<logger::ProjectContext> project_context,
    fidl::InterfaceRequest<LoggerInterface> request,
    fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>* binding_set) {
  binding_set->AddBinding(std::make_unique<LoggerImpl>(
                              std::move(project_context), logger_encoder_, event_aggregator_,
                              observation_writer_, timer_manager_, system_data_, internal_logger_),
                          std::move(request));
}

template <typename LoggerInterface, typename Callback>
void LoggerFactoryImpl::CreateAndBindLogger(
    fuchsia::cobalt::ProjectProfile profile, fidl::InterfaceRequest<LoggerInterface> request,
    Callback callback,
    fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>* binding_set) {
  ReleaseStage release_stage = ToReleaseStageProto(profile.release_stage);
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
  if (factory->is_single_legacy_project()) {
    FX_LOGS(ERROR) << "The provided ProjectProfile contained an older type of "
                      "project that is no longer supported.";
    callback(Status::INVALID_ARGUMENTS);
    return;
  } else if (factory->is_single_project()) {
    BindNewLogger(factory->TakeSingleProjectContext(release_stage), std::move(request),
                  binding_set);
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
void LoggerFactoryImpl::CreateAndBindLoggerFromProjectName(
    std::string project_name, fuchsia::cobalt::ReleaseStage release_stage,
    fidl::InterfaceRequest<LoggerInterface> request, Callback callback,
    fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>* binding_set) {
  auto project_context = global_project_context_factory_->NewProjectContext(
      kFuchsiaCustomerName, project_name, ToReleaseStageProto(release_stage));
  if (!project_context) {
    FX_LOGS(ERROR) << "The CobaltRegistry bundled with this release does not "
                      "include a Fuchsia project named "
                   << project_name;
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

void LoggerFactoryImpl::CreateLoggerFromProjectName(
    std::string project_name, fuchsia::cobalt::ReleaseStage release_stage,
    fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
    CreateLoggerFromProjectNameCallback callback) {
  CreateAndBindLoggerFromProjectName(std::move(project_name), release_stage, std::move(request),
                                     std::move(callback), &logger_bindings_);
}

void LoggerFactoryImpl::CreateLoggerSimpleFromProjectName(
    std::string project_name, fuchsia::cobalt::ReleaseStage release_stage,
    fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
    CreateLoggerSimpleFromProjectNameCallback callback) {
  CreateAndBindLoggerFromProjectName(std::move(project_name), release_stage, std::move(request),
                                     std::move(callback), &logger_simple_bindings_);
}

}  // namespace cobalt
