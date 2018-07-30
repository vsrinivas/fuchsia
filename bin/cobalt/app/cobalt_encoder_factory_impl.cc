// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/cobalt_encoder_factory_impl.h"

#include "lib/fsl/vmo/strings.h"

namespace cobalt {
namespace encoder {

using cobalt::TimerManager;
using config::ClientConfig;

const int32_t kFuchsiaCustomerId = 1;

CobaltEncoderFactoryImpl::CobaltEncoderFactoryImpl(
    std::shared_ptr<ClientConfig> client_config, ClientSecret client_secret,
    ObservationStoreDispatcher* store_dispatcher,
    util::EncryptedMessageMaker* encrypt_to_analyzer,
    ShippingDispatcher* shipping_dispatcher, const SystemData* system_data,
    TimerManager* timer_manager)
    : client_config_(client_config),
      client_secret_(std::move(client_secret)),
      store_dispatcher_(store_dispatcher),
      encrypt_to_analyzer_(encrypt_to_analyzer),
      shipping_dispatcher_(shipping_dispatcher),
      system_data_(system_data),
      timer_manager_(timer_manager) {}

void CobaltEncoderFactoryImpl::GetEncoder(
    int32_t project_id,
    fidl::InterfaceRequest<fuchsia::cobalt::Encoder> request) {
  std::unique_ptr<ProjectContext> project_context(
      new ProjectContext(kFuchsiaCustomerId, project_id, client_config_));

  std::unique_ptr<CobaltEncoderImpl> cobalt_encoder_impl(new CobaltEncoderImpl(
      std::move(project_context), client_secret_, store_dispatcher_,
      encrypt_to_analyzer_, shipping_dispatcher_, system_data_,
      timer_manager_));
  cobalt_encoder_bindings_.AddBinding(std::move(cobalt_encoder_impl),
                                      std::move(request));
}

void CobaltEncoderFactoryImpl::GetEncoderForProject(
    fuchsia::cobalt::ProjectProfile profile,
    fidl::InterfaceRequest<fuchsia::cobalt::Encoder> request,
    GetEncoderForProjectCallback callback) {
  fuchsia::mem::Buffer config_buffer{.vmo = std::move(profile.config.vmo),
                                     .size = profile.config.size};
  fsl::SizedVmo config_vmo;
  bool success =
      fsl::SizedVmo::FromTransport(std::move(config_buffer), &config_vmo);
  if (!success) {
    FXL_LOG(ERROR) << "Transport buffer is invalid";
    callback(Status::INVALID_ARGUMENTS);
    return;
  }

  std::string config_bytes;
  success = fsl::StringFromVmo(config_vmo, &config_bytes);
  if (!success) {
    FXL_LOG(ERROR) << "Could not read Cobalt config from VMO";
    callback(Status::INVALID_ARGUMENTS);
    return;
  }

  std::shared_ptr<config::ClientConfig> project_config;
  auto config_id_pair =
      ClientConfig::CreateFromCobaltProjectConfigBytes(config_bytes);
  project_config.reset(config_id_pair.first.release());
  if (project_config == nullptr) {
    FXL_LOG(ERROR) << "Cobalt config is invalid";
    callback(Status::INVALID_ARGUMENTS);
  }

  std::unique_ptr<ProjectContext> project_context(new ProjectContext(
      kFuchsiaCustomerId, config_id_pair.second, project_config));

  std::unique_ptr<CobaltEncoderImpl> cobalt_encoder_impl(new CobaltEncoderImpl(
      std::move(project_context), client_secret_, store_dispatcher_,
      encrypt_to_analyzer_, shipping_dispatcher_, system_data_,
      timer_manager_));
  cobalt_encoder_bindings_.AddBinding(std::move(cobalt_encoder_impl),
                                      std::move(request));
  callback(Status::OK);
}

}  // namespace encoder
}  // namespace cobalt
