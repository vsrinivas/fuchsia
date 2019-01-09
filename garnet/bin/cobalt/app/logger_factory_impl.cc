// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/logger_factory_impl.h"

#include "garnet/bin/cobalt/app/legacy_logger_impl.h"
#include "garnet/bin/cobalt/app/utils.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fsl/vmo/strings.h"

namespace cobalt {

constexpr char kInternalMetricsProtoPath[] =
    "/pkgfs/packages/cobalt/0/data/cobalt_internal_metrics_registry.pb";

using cobalt::TimerManager;
using config::ClientConfig;
using config::ProjectConfigs;
using fuchsia::cobalt::ProjectProfile;
using fuchsia::cobalt::Status;

const int32_t kFuchsiaCustomerId = 1;
constexpr char kFuchsiaCustomerName[] = "fuchsia";

namespace {
std::pair<std::unique_ptr<ClientConfig>, uint32_t> GetClientConfig(
    ProjectProfile profile) {
  fsl::SizedVmo config_vmo;
  bool success =
      fsl::SizedVmo::FromTransport(std::move(profile.config), &config_vmo);
  if (!success) {
    FXL_LOG(ERROR) << "Transport buffer is invalid";
    return std::make_pair(nullptr, 0);
  }

  std::string config_bytes;
  success = fsl::StringFromVmo(config_vmo, &config_bytes);
  if (!success) {
    FXL_LOG(ERROR) << "Could not read Cobalt config from VMO";
    return std::make_pair(nullptr, 0);
  }

  return ClientConfig::CreateFromCobaltProjectRegistryBytes(config_bytes);
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
std::pair<std::unique_ptr<encoder::ProjectContext>,
          std::unique_ptr<logger::ProjectContext>>
CreateProjectContexts(ProjectProfile profile) {
  auto release_stage = profile.release_stage;
  auto config_id_pair = GetClientConfig(std::move(profile));

  std::shared_ptr<config::ClientConfig> project_config;
  project_config.reset(config_id_pair.first.release());
  if (project_config == nullptr) {
    FXL_LOG(ERROR) << "Cobalt config is invalid";
    return std::pair(nullptr, nullptr);
  }

  if (project_config->IsLegacy()) {
    std::unique_ptr<encoder::ProjectContext> project_context(
        new encoder::ProjectContext(kFuchsiaCustomerId, config_id_pair.second,
                                    project_config));
    return std::pair(std::move(project_context), nullptr);
  } else {
    auto customer_cfg = project_config->TakeCustomerConfig();
    auto project_cfg = customer_cfg->mutable_projects(0);
    auto metrics = std::make_unique<MetricDefinitions>();
    metrics->mutable_metric()->Swap(project_cfg->mutable_metrics());
    return std::pair(
        nullptr, std::make_unique<logger::ProjectContext>(
                     customer_cfg->customer_id(), project_cfg->project_id(),
                     customer_cfg->customer_name(), project_cfg->project_name(),
                     std::move(metrics), ToReleaseStageProto(release_stage)));
  }
}
}  // namespace

LoggerFactoryImpl::LoggerFactoryImpl(
    encoder::ClientSecret client_secret,
    encoder::ObservationStore* legacy_observation_store,
    util::EncryptedMessageMaker* legacy_encrypt_to_analyzer,
    encoder::ShippingManager* legacy_shipping_manager,
    const encoder::SystemData* system_data, TimerManager* timer_manager,
    logger::Encoder* logger_encoder,
    logger::ObservationWriter* observation_writer,
    logger::EventAggregator* event_aggregator,
    std::shared_ptr<config::ClientConfig> client_config,
    std::shared_ptr<ProjectConfigs> project_configs)
    : client_secret_(std::move(client_secret)),
      legacy_observation_store_(legacy_observation_store),
      legacy_encrypt_to_analyzer_(legacy_encrypt_to_analyzer),
      legacy_shipping_manager_(legacy_shipping_manager),
      system_data_(system_data),
      timer_manager_(timer_manager),
      logger_encoder_(logger_encoder),
      observation_writer_(observation_writer),
      event_aggregator_(event_aggregator),
      client_config_(client_config),
      project_configs_(project_configs) {
  ProjectProfile profile;
  fsl::SizedVmo config_vmo;
  fsl::VmoFromFilename(kInternalMetricsProtoPath, &config_vmo);
  profile.config = std::move(config_vmo).ToTransport();

  auto [legacy_project_context, project_context] =
      CreateProjectContexts(std::move(profile));
  internal_project_context_ = std::move(project_context);
  internal_logger_.reset(new logger::Logger(
      logger_encoder_, event_aggregator_, observation_writer_,
      internal_project_context_.get(), nullptr));
}

void LoggerFactoryImpl::CreateLogger(
    ProjectProfile profile,
    fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
    CreateLoggerCallback callback) {
  auto [legacy_project_context, project_context] =
      CreateProjectContexts(std::move(profile));
  if (legacy_project_context) {
    logger_bindings_.AddBinding(
        std::make_unique<LegacyLoggerImpl>(
            std::move(legacy_project_context), client_secret_,
            legacy_observation_store_, legacy_encrypt_to_analyzer_,
            legacy_shipping_manager_, system_data_, timer_manager_),
        std::move(request));
    callback(Status::OK);
  } else if (project_context) {
    logger_bindings_.AddBinding(
        std::make_unique<LoggerImpl>(
            std::move(project_context), logger_encoder_, event_aggregator_,
            observation_writer_, timer_manager_, internal_logger_.get()),
        std::move(request));
    callback(Status::OK);
  } else {
    callback(Status::INVALID_ARGUMENTS);
  }
}

void LoggerFactoryImpl::CreateLoggerSimple(
    ProjectProfile profile,
    fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
    CreateLoggerSimpleCallback callback) {
  auto [legacy_project_context, project_context] =
      CreateProjectContexts(std::move(profile));

  if (legacy_project_context) {
    logger_simple_bindings_.AddBinding(
        std::make_unique<LegacyLoggerImpl>(
            std::move(legacy_project_context), client_secret_,
            legacy_observation_store_, legacy_encrypt_to_analyzer_,
            legacy_shipping_manager_, system_data_, timer_manager_),
        std::move(request));
    callback(Status::OK);
  } else if (project_context) {
    logger_simple_bindings_.AddBinding(
        std::make_unique<LoggerImpl>(
            std::move(project_context), logger_encoder_, event_aggregator_,
            observation_writer_, timer_manager_, internal_logger_.get()),
        std::move(request));
    callback(Status::OK);
  } else {
    callback(Status::INVALID_ARGUMENTS);
  }
}

void LoggerFactoryImpl::CreateLoggerFromProjectName(
    std::string project_name, fuchsia::cobalt::ReleaseStage release_stage,
    fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
    CreateLoggerFromProjectNameCallback callback) {
  auto project_context_or = logger::ProjectContext::ConstructWithProjectConfigs(
      kFuchsiaCustomerName, project_name, project_configs_,
      ToReleaseStageProto(release_stage));

  if (!project_context_or.ok()) {
    FXL_LOG(ERROR) << "Failed to construct ProjectContext from ProjectConfigs: "
                   << project_context_or.status().error_message();
    callback(ToCobaltStatus(project_context_or.status()));
    return;
  }

  logger_bindings_.AddBinding(
      std::make_unique<LoggerImpl>(project_context_or.ConsumeValueOrDie(),
                                   logger_encoder_, event_aggregator_,
                                   observation_writer_, timer_manager_,
                                   internal_logger_.get()),
      std::move(request));
  callback(Status::OK);
}

void LoggerFactoryImpl::CreateLoggerSimpleFromProjectName(
    std::string project_name, fuchsia::cobalt::ReleaseStage release_stage,
    fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
    CreateLoggerSimpleFromProjectNameCallback callback) {
  auto project_context_or = logger::ProjectContext::ConstructWithProjectConfigs(
      kFuchsiaCustomerName, project_name, project_configs_,
      ToReleaseStageProto(release_stage));

  if (!project_context_or.ok()) {
    FXL_LOG(ERROR) << "Failed to construct ProjectContext from ProjectConfigs: "
                   << project_context_or.status().error_message();
    callback(ToCobaltStatus(project_context_or.status()));
    return;
  }

  logger_simple_bindings_.AddBinding(
      std::make_unique<LoggerImpl>(project_context_or.ConsumeValueOrDie(),
                                   logger_encoder_, event_aggregator_,
                                   observation_writer_, timer_manager_,
                                   internal_logger_.get()),
      std::move(request));
  callback(Status::OK);
}

void LoggerFactoryImpl::CreateLoggerFromProjectId(
    uint32_t project_id, fuchsia::cobalt::ReleaseStage release_stage,
    fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
    CreateLoggerFromProjectIdCallback callback) {
  auto project_context = std::make_unique<encoder::ProjectContext>(
      kFuchsiaCustomerId, project_id, client_config_);

  logger_bindings_.AddBinding(
      std::make_unique<LegacyLoggerImpl>(
          std::move(project_context), client_secret_, legacy_observation_store_,
          legacy_encrypt_to_analyzer_, legacy_shipping_manager_, system_data_,
          timer_manager_),
      std::move(request));

  callback(Status::OK);
}

void LoggerFactoryImpl::CreateLoggerSimpleFromProjectId(
    uint32_t project_id, fuchsia::cobalt::ReleaseStage release_stage,
    fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
    CreateLoggerSimpleFromProjectIdCallback callback) {
  auto project_context = std::make_unique<encoder::ProjectContext>(
      kFuchsiaCustomerId, project_id, client_config_);

  logger_simple_bindings_.AddBinding(
      std::make_unique<LegacyLoggerImpl>(
          std::move(project_context), client_secret_, legacy_observation_store_,
          legacy_encrypt_to_analyzer_, legacy_shipping_manager_, system_data_,
          timer_manager_),
      std::move(request));

  callback(Status::OK);
}

}  // namespace cobalt
