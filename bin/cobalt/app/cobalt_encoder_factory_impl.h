// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_APP_COBALT_ENCODER_FACTORY_IMPL_H_
#define GARNET_BIN_COBALT_APP_COBALT_ENCODER_FACTORY_IMPL_H_

#include <stdlib.h>

#include <fuchsia/cobalt/cpp/fidl.h>

#include "garnet/bin/cobalt/app/cobalt_encoder_impl.h"
#include "garnet/bin/cobalt/app/timer_manager.h"
#include "lib/fidl/cpp/binding_set.h"
#include "third_party/cobalt/config/client_config.h"
#include "third_party/cobalt/encoder/observation_store_dispatcher.h"
#include "third_party/cobalt/encoder/shipping_dispatcher.h"
#include "third_party/cobalt/util/encrypted_message_util.h"

namespace cobalt {
namespace encoder {

class CobaltEncoderFactoryImpl : public fuchsia::cobalt::EncoderFactory {
 public:
  CobaltEncoderFactoryImpl(std::shared_ptr<config::ClientConfig> client_config,
                           ClientSecret client_secret,
                           ObservationStoreDispatcher* store_dispatcher,
                           util::EncryptedMessageMaker* encrypt_to_analyzer,
                           ShippingDispatcher* shipping_dispatcher,
                           const SystemData* system_data,
                           TimerManager* timer_manager);

 private:
  void GetEncoder(int32_t project_id,
                  fidl::InterfaceRequest<fuchsia::cobalt::Encoder> request);

  void GetEncoderForProject(
      fuchsia::cobalt::ProjectProfile profile,
      fidl::InterfaceRequest<fuchsia::cobalt::Encoder> request,
      GetEncoderForProjectCallback callback);

  std::shared_ptr<config::ClientConfig> client_config_;
  ClientSecret client_secret_;
  fidl::BindingSet<fuchsia::cobalt::Encoder,
                   std::unique_ptr<fuchsia::cobalt::Encoder>>
      cobalt_encoder_bindings_;
  ObservationStoreDispatcher* store_dispatcher_;      // not owned
  util::EncryptedMessageMaker* encrypt_to_analyzer_;  // not owned
  ShippingDispatcher* shipping_dispatcher_;           // not owned
  const SystemData* system_data_;                     // not owned
  TimerManager* timer_manager_;                       // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltEncoderFactoryImpl);
};

}  // namespace encoder
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_APP_COBALT_ENCODER_FACTORY_IMPL_H_
