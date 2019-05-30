// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_LOGGER_FACTORY_IMPL_H_
#define SRC_COBALT_BIN_APP_LOGGER_FACTORY_IMPL_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <stdlib.h>

#include "lib/fidl/cpp/binding_set.h"
#include "src/cobalt/bin/app/logger_impl.h"
#include "src/cobalt/bin/app/timer_manager.h"
#include "third_party/cobalt/encoder/observation_store.h"
#include "third_party/cobalt/encoder/shipping_manager.h"
#include "third_party/cobalt/logger/encoder.h"
#include "third_party/cobalt/logger/event_aggregator.h"
#include "third_party/cobalt/logger/observation_writer.h"
#include "third_party/cobalt/logger/project_context_factory.h"
#include "third_party/cobalt/util/encrypted_message_util.h"

namespace cobalt {

class LoggerFactoryImpl : public fuchsia::cobalt::LoggerFactory {
 public:
  LoggerFactoryImpl(const std::string& global_cobalt_registry_bytes,
                    encoder::ClientSecret client_secret,
                    TimerManager* timer_manager,
                    logger::Encoder* logger_encoder,
                    logger::ObservationWriter* observation_writer,
                    logger::EventAggregator* event_aggregator);

 private:
  // Constructs a new LoggerImpl based on |project_context|, binds it to
  // |request|, and stores the binding in |binding_set|.
  // |LoggerInterface| should be Logger or LoggerSimple.
  template <typename LoggerInterface>
  void BindNewLogger(
      std::unique_ptr<logger::ProjectContext> project_context,
      fidl::InterfaceRequest<LoggerInterface> request,
      fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>*
          binding_set);

  // Attempts to extract a CobaltRegistry containing a single project from
  // |profile| and uses this to construct a LoggerImpl. Binds this to |request|
  // and stores the binding in |binding_set|. |callback| will be invoked with
  // OK upon success or an error status otherwise. |LoggerInterface| should be
  // Logger or LoggerSimple.
  template <typename LoggerInterface, typename Callback>
  void CreateAndBindLogger(
      fuchsia::cobalt::ProjectProfile profile,
      fidl::InterfaceRequest<LoggerInterface> request, Callback callback,
      fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>*
          binding_set);

  // Extracts the Cobalt 1.0 project with the given |project_name| from the
  // global CobaltRegistry, if there is such a project in the registry, and
  // uses this, and |release_stage|, to construct a  LoggerImpl. Binds this to
  // |request| and stores the binding in |binding_set|. |callback| will be
  // invoked with OK upon success or an error status otherwise.
  // |LoggerInterface| should be Logger or LoggerSimple.
  template <typename LoggerInterface, typename Callback>
  void CreateAndBindLoggerFromProjectName(
      std::string project_name, fuchsia::cobalt::ReleaseStage release_stage,
      fidl::InterfaceRequest<LoggerInterface> request, Callback callback,
      fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>*
          binding_set);

  void CreateLogger(fuchsia::cobalt::ProjectProfile profile,
                    fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
                    CreateLoggerCallback callback);

  void CreateLoggerSimple(
      fuchsia::cobalt::ProjectProfile profile,
      fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
      CreateLoggerSimpleCallback callback);

  void CreateLoggerFromProjectName(
      std::string project_name, fuchsia::cobalt::ReleaseStage stage,
      fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
      CreateLoggerFromProjectNameCallback callback);

  void CreateLoggerSimpleFromProjectName(
      std::string project_name, fuchsia::cobalt::ReleaseStage stage,
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

  // Cobalt uses internal_logger_ to log events about Cobalt.
  std::unique_ptr<logger::Logger> internal_logger_;

  TimerManager* timer_manager_;                    // not owned
  logger::Encoder* logger_encoder_;                // not owned
  logger::ObservationWriter* observation_writer_;  // not owned
  logger::EventAggregator* event_aggregator_;      // not owned
  std::shared_ptr<cobalt::logger::ProjectContextFactory>
      global_project_context_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LoggerFactoryImpl);
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_LOGGER_FACTORY_IMPL_H_
