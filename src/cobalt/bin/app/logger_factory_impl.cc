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
constexpr char kCobaltInternalCustomerName[] = "cobalt_internal";
constexpr char kCobaltInternalMetricsProjectName[] = "metrics";

namespace {
bool ExtractCobaltRegistryBytes(ProjectProfile profile,
                                std::string* metrics_registry_bytes_out) {
  CHECK(metrics_registry_bytes_out);
  fsl::SizedVmo registry_vmo;
  bool success =
      fsl::SizedVmo::FromTransport(std::move(profile.config), &registry_vmo);
  if (!success) {
    FXL_LOG(ERROR) << "Transport buffer is invalid";
    return false;
  }

  success = fsl::StringFromVmo(registry_vmo, metrics_registry_bytes_out);
  if (!success) {
    FXL_LOG(ERROR) << "Could not read Metrics Registry from VMO";
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
      FXL_LOG(ERROR) << "Unknown ReleaseStage provided. Defaulting to DEBUG.";
      return ReleaseStage::DEBUG;
  }
}
}  // namespace

LoggerFactoryImpl::LoggerFactoryImpl(
    const std::string& global_cobalt_registry_bytes,
    encoder::ClientSecret client_secret, TimerManager* timer_manager,
    logger::Encoder* logger_encoder,
    logger::ObservationWriter* observation_writer,
    logger::EventAggregator* event_aggregator)
    : client_secret_(std::move(client_secret)),
      timer_manager_(timer_manager),
      logger_encoder_(logger_encoder),
      observation_writer_(observation_writer),
      event_aggregator_(event_aggregator),
      global_project_context_factory_(
          new ProjectContextFactory(global_cobalt_registry_bytes)) {
  auto internal_project_context =
      global_project_context_factory_->NewProjectContext(
          kCobaltInternalCustomerName, kCobaltInternalMetricsProjectName,
          ReleaseStage::GA);
  if (!internal_project_context) {
    FXL_LOG(ERROR) << "The CobaltRegistry bundled with Cobalt does not "
                      "include the expected internal metrics project. "
                      "Cobalt-measuring-Cobalt will be disabled.";
  }

  // Help the compiler understand which of several constructor overloads we
  // mean to be invoking here.
  logger::LoggerInterface* null_logger = nullptr;
  internal_logger_.reset(new logger::Logger(std::move(internal_project_context),
                                            logger_encoder_, event_aggregator_,
                                            observation_writer_, null_logger));
}

template <typename LoggerInterface>
void LoggerFactoryImpl::BindNewLogger(
    std::unique_ptr<logger::ProjectContext> project_context,
    fidl::InterfaceRequest<LoggerInterface> request,
    fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>*
        binding_set) {
  binding_set->AddBinding(
      std::make_unique<LoggerImpl>(std::move(project_context), logger_encoder_,
                                   event_aggregator_, observation_writer_,
                                   timer_manager_, internal_logger_.get()),
      std::move(request));
}

template <typename LoggerInterface, typename Callback>
void LoggerFactoryImpl::CreateAndBindLogger(
    fuchsia::cobalt::ProjectProfile profile,
    fidl::InterfaceRequest<LoggerInterface> request, Callback callback,
    fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>*
        binding_set) {
  ReleaseStage release_stage = ToReleaseStageProto(profile.release_stage);
  std::string cobalt_registry_bytes;
  if (!ExtractCobaltRegistryBytes(std::move(profile), &cobalt_registry_bytes)) {
    FXL_LOG(ERROR) << "Unable to extract a CobaltRegistry from the provided "
                      "ProjectProfile.";
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  std::shared_ptr<ProjectContextFactory> factory(
      new ProjectContextFactory(cobalt_registry_bytes));
  if (!factory->is_valid()) {
    FXL_LOG(ERROR) << "Unable to extract a valid CobaltRegistry from the "
                      "provided ProjectProfile.";
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  if (factory->is_single_legacy_project()) {
    FXL_LOG(ERROR) << "The provided ProjectProfile contained an older type of "
                      "project that is no longer supported.";
    callback(Status::INVALID_ARGUMENTS);
    return;
  } else if (factory->is_single_project()) {
    BindNewLogger(factory->TakeSingleProjectContext(release_stage),
                  std::move(request), binding_set);
    callback(Status::OK);
    return;
  } else {
    FXL_LOG(ERROR) << "The CobaltRegistry in the provided ProjectProfile was "
                      "invalid because it contained multiple projects.";
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
}

template <typename LoggerInterface, typename Callback>
void LoggerFactoryImpl::CreateAndBindLoggerFromProjectName(
    std::string project_name, fuchsia::cobalt::ReleaseStage release_stage,
    fidl::InterfaceRequest<LoggerInterface> request, Callback callback,
    fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>*
        binding_set) {
  auto project_context = global_project_context_factory_->NewProjectContext(
      kFuchsiaCustomerName, project_name, ToReleaseStageProto(release_stage));
  if (!project_context) {
    FXL_LOG(ERROR) << "The CobaltRegistry bundled with this release does not "
                      "include a Fuchsia project named "
                   << project_name;
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  BindNewLogger(std::move(project_context), std::move(request), binding_set);
  callback(Status::OK);
}

void LoggerFactoryImpl::CreateLogger(
    ProjectProfile profile,
    fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
    CreateLoggerCallback callback) {
  CreateAndBindLogger(std::move(profile), std::move(request),
                      std::move(callback), &logger_bindings_);
}

void LoggerFactoryImpl::CreateLoggerSimple(
    ProjectProfile profile,
    fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
    CreateLoggerSimpleCallback callback) {
  CreateAndBindLogger(std::move(profile), std::move(request),
                      std::move(callback), &logger_simple_bindings_);
}

void LoggerFactoryImpl::CreateLoggerFromProjectName(
    std::string project_name, fuchsia::cobalt::ReleaseStage release_stage,
    fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
    CreateLoggerFromProjectNameCallback callback) {
  CreateAndBindLoggerFromProjectName(std::move(project_name), release_stage,
                                     std::move(request), std::move(callback),
                                     &logger_bindings_);
}

void LoggerFactoryImpl::CreateLoggerSimpleFromProjectName(
    std::string project_name, fuchsia::cobalt::ReleaseStage release_stage,
    fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
    CreateLoggerSimpleFromProjectNameCallback callback) {
  CreateAndBindLoggerFromProjectName(std::move(project_name), release_stage,
                                     std::move(request), std::move(callback),
                                     &logger_simple_bindings_);
}

void LoggerFactoryImpl::CreateLoggerFromProjectId(
    uint32_t project_id, fuchsia::cobalt::ReleaseStage release_stage,
    fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
    CreateLoggerFromProjectIdCallback callback) {
  FXL_LOG(ERROR)
      << "The method CreateLoggerFromProjectId() is no longer supported",
      callback(Status::INVALID_ARGUMENTS);
}

void LoggerFactoryImpl::CreateLoggerSimpleFromProjectId(
    uint32_t project_id, fuchsia::cobalt::ReleaseStage release_stage,
    fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
    CreateLoggerSimpleFromProjectIdCallback callback) {
  FXL_LOG(ERROR)
      << "The method CreateLoggerSimpleFromProjectId() is no longer supported",
      callback(Status::INVALID_ARGUMENTS);
}

}  // namespace cobalt
