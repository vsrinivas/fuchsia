// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_APP_LOGGER_FACTORY_IMPL_H_
#define GARNET_BIN_COBALT_APP_LOGGER_FACTORY_IMPL_H_

#include <stdlib.h>

#include <fuchsia/cobalt/cpp/fidl.h>

#include "garnet/bin/cobalt/app/logger_impl.h"
#include "garnet/bin/cobalt/app/timer_manager.h"
#include "lib/fidl/cpp/binding_set.h"
#include "third_party/cobalt/config/client_config.h"
#include "third_party/cobalt/config/project_configs.h"
#include "third_party/cobalt/encoder/observation_store.h"
#include "third_party/cobalt/encoder/shipping_manager.h"
#include "third_party/cobalt/logger/encoder.h"
#include "third_party/cobalt/logger/observation_writer.h"
#include "third_party/cobalt/util/encrypted_message_util.h"

namespace cobalt {

class LoggerFactoryImpl : public fuchsia::cobalt::LoggerFactory {
 public:
  LoggerFactoryImpl(encoder::ClientSecret client_secret,
                    encoder::ObservationStore* observation_store,
                    util::EncryptedMessageMaker* encrypt_to_analyzer,
                    encoder::ShippingManager* shipping_manager,
                    const encoder::SystemData* system_data,
                    TimerManager* timer_manager,
                    logger::Encoder* logger_encoder,
                    logger::ObservationWriter* observation_writer,
                    std::shared_ptr<config::ClientConfig> client_config,
                    std::shared_ptr<config::ProjectConfigs> project_configs);

 private:
  void CreateLogger(fuchsia::cobalt::ProjectProfile profile,
                    fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
                    CreateLoggerCallback callback);

  void CreateLoggerSimple(
      fuchsia::cobalt::ProjectProfile profile,
      fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
      CreateLoggerSimpleCallback callback);

  void CreateLoggerFromProjectName(
      fidl::StringPtr project_name, fuchsia::cobalt::ReleaseStage stage,
      fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
      CreateLoggerFromProjectNameCallback callback);

  void CreateLoggerSimpleFromProjectName(
      fidl::StringPtr project_name, fuchsia::cobalt::ReleaseStage stage,
      fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
      CreateLoggerSimpleFromProjectNameCallback callback);

  void CreateLoggerFromProjectId(
      uint32_t project_id, fuchsia::cobalt::ReleaseStage stage,
      fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
      CreateLoggerFromProjectIdCallback callback);

  void CreateLoggerSimpleFromProjectId(
      uint32_t project_id, fuchsia::cobalt::ReleaseStage stage,
      fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
      CreateLoggerSimpleFromProjectIdCallback callback);

  encoder::ClientSecret client_secret_;
  fidl::BindingSet<fuchsia::cobalt::Logger,
                   std::unique_ptr<fuchsia::cobalt::Logger>>
      logger_bindings_;
  fidl::BindingSet<fuchsia::cobalt::LoggerSimple,
                   std::unique_ptr<fuchsia::cobalt::LoggerSimple>>
      logger_simple_bindings_;

  // The owned copy of the ProjectContext for internal_logger_.
  // TODO(zmbush): Update logger::Logger to own its ProjectContext.
  std::unique_ptr<logger::ProjectContext> internal_project_context_;

  // Cobalt uses internal_logger_ to log events about Cobalt.
  std::unique_ptr<logger::Logger> internal_logger_;

  encoder::ObservationStore* observation_store_;      // not owned
  util::EncryptedMessageMaker* encrypt_to_analyzer_;  // not owned
  encoder::ShippingManager* shipping_manager_;        // not owned
  const encoder::SystemData* system_data_;            // not owned
  TimerManager* timer_manager_;                       // not owned
  logger::Encoder* logger_encoder_;                   // not owned
  logger::ObservationWriter* observation_writer_;     // not owned

  // Used for cobalt v0.1 clients.
  std::shared_ptr<config::ClientConfig> client_config_;

  // Used for cobalt v1.0 clients.
  std::shared_ptr<config::ProjectConfigs> project_configs_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LoggerFactoryImpl);
};

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_APP_LOGGER_FACTORY_IMPL_H_
