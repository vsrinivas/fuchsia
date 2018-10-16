// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/logger_factory_impl.h"

#include "garnet/bin/cobalt/app/legacy_logger_impl.h"
#include "garnet/bin/cobalt/app/logger_impl.h"
#include "lib/fsl/vmo/strings.h"

namespace cobalt {

using cobalt::TimerManager;
using config::ClientConfig;
using fuchsia::cobalt::ProjectProfile;
using fuchsia::cobalt::Status;

const int32_t kFuchsiaCustomerId = 1;

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

  return ClientConfig::CreateFromCobaltProjectConfigBytes(config_bytes);
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
    ReleaseStage cobalt_release_stage;
    switch (release_stage) {
      case fuchsia::cobalt::ReleaseStage::GA:
        cobalt_release_stage = ReleaseStage::GA;
        break;
      case fuchsia::cobalt::ReleaseStage::DOGFOOD:
        cobalt_release_stage = ReleaseStage::DOGFOOD;
        break;
      case fuchsia::cobalt::ReleaseStage::FISHFOOD:
        cobalt_release_stage = ReleaseStage::FISHFOOD;
        break;
      case fuchsia::cobalt::ReleaseStage::DEBUG:
        cobalt_release_stage = ReleaseStage::DEBUG;
        break;
      default:
        FXL_LOG(ERROR) << "Unknown ReleaseStage provided";
        return std::pair(nullptr, nullptr);
    }
    return std::pair(
        nullptr, std::make_unique<logger::ProjectContext>(
                     customer_cfg->customer_id(), project_cfg->project_id(),
                     customer_cfg->customer_name(), project_cfg->project_name(),
                     std::move(metrics), cobalt_release_stage));
  }
}
}  // namespace

LoggerFactoryImpl::LoggerFactoryImpl(
    encoder::ClientSecret client_secret,
    encoder::ObservationStore* observation_store,
    util::EncryptedMessageMaker* encrypt_to_analyzer,
    encoder::ShippingManager* shipping_manager,
    const encoder::SystemData* system_data, TimerManager* timer_manager,
    logger::Encoder* logger_encoder,
    logger::ObservationWriter* observation_writer)
    : client_secret_(std::move(client_secret)),
      observation_store_(observation_store),
      encrypt_to_analyzer_(encrypt_to_analyzer),
      shipping_manager_(shipping_manager),
      system_data_(system_data),
      timer_manager_(timer_manager),
      logger_encoder_(logger_encoder),
      observation_writer_(observation_writer) {}

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
            observation_store_, encrypt_to_analyzer_, shipping_manager_,
            system_data_, timer_manager_),
        std::move(request));
    callback(Status::OK);
  } else if (project_context) {
    logger_bindings_.AddBinding(std::make_unique<LoggerImpl>(
                                    std::move(project_context), logger_encoder_,
                                    observation_writer_, timer_manager_),
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
            observation_store_, encrypt_to_analyzer_, shipping_manager_,
            system_data_, timer_manager_),
        std::move(request));
    callback(Status::OK);
  } else if (project_context) {
    logger_simple_bindings_.AddBinding(
        std::make_unique<LoggerImpl>(std::move(project_context),
                                     logger_encoder_, observation_writer_,
                                     timer_manager_),
        std::move(request));
    callback(Status::OK);
  } else {
    callback(Status::INVALID_ARGUMENTS);
  }
}

}  // namespace cobalt
