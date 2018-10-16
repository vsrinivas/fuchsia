// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/logger_factory_impl.h"

#include "lib/fsl/vmo/strings.h"

namespace cobalt {

using cobalt::TimerManager;
using config::ClientConfig;
using fuchsia::cobalt::ProjectProfile;
using fuchsia::cobalt::Status;

const int32_t kFuchsiaCustomerId = 1;

namespace {
std::unique_ptr<encoder::ProjectContext> CreateProjectContext(
    ProjectProfile profile) {
  fsl::SizedVmo config_vmo;
  bool success =
      fsl::SizedVmo::FromTransport(std::move(profile.config), &config_vmo);
  if (!success) {
    FXL_LOG(ERROR) << "Transport buffer is invalid";
    return nullptr;
  }

  std::string config_bytes;
  success = fsl::StringFromVmo(config_vmo, &config_bytes);
  if (!success) {
    FXL_LOG(ERROR) << "Could not read Cobalt config from VMO";
    return nullptr;
  }

  std::shared_ptr<config::ClientConfig> project_config;
  auto config_id_pair =
      ClientConfig::CreateFromCobaltProjectConfigBytes(config_bytes);
  project_config.reset(config_id_pair.first.release());
  if (project_config == nullptr) {
    FXL_LOG(ERROR) << "Cobalt config is invalid";
    return nullptr;
  }

  std::unique_ptr<encoder::ProjectContext> project_context(
      new encoder::ProjectContext(kFuchsiaCustomerId, config_id_pair.second,
                                  project_config));
  return project_context;
}
}  // namespace

LoggerFactoryImpl::LoggerFactoryImpl(
    encoder::ClientSecret client_secret,
    encoder::ObservationStore* observation_store,
    util::EncryptedMessageMaker* encrypt_to_analyzer,
    encoder::ShippingManager* shipping_manager,
    const encoder::SystemData* system_data, TimerManager* timer_manager)
    : client_secret_(std::move(client_secret)),
      observation_store_(observation_store),
      encrypt_to_analyzer_(encrypt_to_analyzer),
      shipping_manager_(shipping_manager),
      system_data_(system_data),
      timer_manager_(timer_manager) {}

void LoggerFactoryImpl::CreateLogger(
    ProjectProfile profile,
    fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
    CreateLoggerCallback callback) {
  auto project_context = CreateProjectContext(std::move(profile));
  if (!project_context) {
    callback(Status::INVALID_ARGUMENTS);
    return;
  }

  std::unique_ptr<LegacyLoggerImpl> logger_impl(new LegacyLoggerImpl(
      std::move(project_context), client_secret_, observation_store_,
      encrypt_to_analyzer_, shipping_manager_, system_data_, timer_manager_));
  logger_bindings_.AddBinding(std::move(logger_impl), std::move(request));
  callback(Status::OK);
}

void LoggerFactoryImpl::CreateLoggerSimple(
    ProjectProfile profile,
    fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
    CreateLoggerSimpleCallback callback) {
  auto project_context = CreateProjectContext(std::move(profile));
  if (!project_context) {
    callback(Status::INVALID_ARGUMENTS);
    return;
  }

  std::unique_ptr<LegacyLoggerImpl> logger_impl(new LegacyLoggerImpl(
      std::move(project_context), client_secret_, observation_store_,
      encrypt_to_analyzer_, shipping_manager_, system_data_, timer_manager_));
  logger_simple_bindings_.AddBinding(std::move(logger_impl),
                                     std::move(request));
  callback(Status::OK);
}

}  // namespace cobalt
