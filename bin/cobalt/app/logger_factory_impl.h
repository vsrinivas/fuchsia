// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_APP_LOGGER_FACTORY_IMPL_H_
#define GARNET_BIN_COBALT_APP_LOGGER_FACTORY_IMPL_H_

#include <stdlib.h>

#include <fuchsia/cobalt/cpp/fidl.h>

#include "garnet/bin/cobalt/app/legacy_logger_impl.h"
#include "garnet/bin/cobalt/app/timer_manager.h"
#include "lib/fidl/cpp/binding_set.h"
#include "third_party/cobalt/encoder/observation_store.h"
#include "third_party/cobalt/encoder/shipping_manager.h"
#include "third_party/cobalt/util/encrypted_message_util.h"

namespace cobalt {

class LoggerFactoryImpl : public fuchsia::cobalt::LoggerFactory {
 public:
  LoggerFactoryImpl(encoder::ClientSecret client_secret,
                    encoder::ObservationStore* observation_store,
                    util::EncryptedMessageMaker* encrypt_to_analyzer,
                    encoder::ShippingManager* shipping_manager,
                    const encoder::SystemData* system_data,
                    TimerManager* timer_manager);

 private:
  void CreateLogger(fuchsia::cobalt::ProjectProfile profile,
                    fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
                    CreateLoggerCallback callback);

  void CreateLoggerSimple(
      fuchsia::cobalt::ProjectProfile profile,
      fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
      CreateLoggerSimpleCallback callback);

  encoder::ClientSecret client_secret_;
  fidl::BindingSet<fuchsia::cobalt::Logger,
                   std::unique_ptr<fuchsia::cobalt::Logger>>
      logger_bindings_;
  fidl::BindingSet<fuchsia::cobalt::LoggerSimple,
                   std::unique_ptr<fuchsia::cobalt::LoggerSimple>>
      logger_simple_bindings_;
  encoder::ObservationStore* observation_store_;      // not owned
  util::EncryptedMessageMaker* encrypt_to_analyzer_;  // not owned
  encoder::ShippingManager* shipping_manager_;        // not owned
  const encoder::SystemData* system_data_;            // not owned
  TimerManager* timer_manager_;                       // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(LoggerFactoryImpl);
};

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_APP_LOGGER_FACTORY_IMPL_H_
