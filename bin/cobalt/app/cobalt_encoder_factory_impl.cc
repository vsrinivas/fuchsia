// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/cobalt_encoder_factory_impl.h"

namespace cobalt {
namespace encoder {

using cobalt::TimerManager;
using config::ClientConfig;
using fuchsia::cobalt::CobaltEncoder;

const int32_t kFuchsiaCustomerId = 1;

CobaltEncoderFactoryImpl::CobaltEncoderFactoryImpl(
    std::shared_ptr<ClientConfig> client_config, ClientSecret client_secret,
    ShippingDispatcher* shipping_dispatcher, const SystemData* system_data,
    TimerManager* timer_manager)
    : client_config_(client_config),
      client_secret_(std::move(client_secret)),
      shipping_dispatcher_(shipping_dispatcher),
      system_data_(system_data),
      timer_manager_(timer_manager) {}

void CobaltEncoderFactoryImpl::GetEncoder(
    int32_t project_id, fidl::InterfaceRequest<CobaltEncoder> request) {
  std::unique_ptr<ProjectContext> project_context(
      new ProjectContext(kFuchsiaCustomerId, project_id, client_config_));

  std::unique_ptr<CobaltEncoderImpl> cobalt_encoder_impl(new CobaltEncoderImpl(
      std::move(project_context), client_secret_, shipping_dispatcher_,
      system_data_, timer_manager_));
  cobalt_encoder_bindings_.AddBinding(std::move(cobalt_encoder_impl),
                                      std::move(request));
}

void CobaltEncoderFactoryImpl::GetEncoderForConfig(
    fidl::StringPtr config, fidl::InterfaceRequest<CobaltEncoder> request) {
  // Implementation is on the way
}

}  // namespace encoder
}  // namespace cobalt
