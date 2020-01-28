// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/logger_factory_impl.h"

#include "src/cobalt/bin/app/utils.h"
#include "src/lib/fsl/vmo/strings.h"

namespace cobalt {

using cobalt::TimerManager;
using cobalt::logger::ProjectContextFactory;
using config::ProjectConfigs;
using fuchsia::cobalt::Status;

constexpr uint32_t kFuchsiaCustomerId = 1;

LoggerFactoryImpl::LoggerFactoryImpl(
    std::shared_ptr<cobalt::logger::ProjectContextFactory> global_project_context_factory,
    encoder::ClientSecret client_secret, TimerManager* timer_manager,
    logger::Encoder* logger_encoder, logger::ObservationWriter* observation_writer,
    local_aggregation::EventAggregator* event_aggregator,
    util::ValidatedClockInterface* validated_clock,
    std::weak_ptr<logger::UndatedEventManager> undated_event_manager,
    logger::Logger* internal_logger, encoder::SystemDataInterface* system_data)
    : client_secret_(std::move(client_secret)),
      global_project_context_factory_(std::move(global_project_context_factory)),
      timer_manager_(timer_manager),
      logger_encoder_(logger_encoder),
      observation_writer_(observation_writer),
      event_aggregator_(event_aggregator),
      validated_clock_(validated_clock),
      undated_event_manager_(std::move(undated_event_manager)),
      internal_logger_(internal_logger),
      system_data_(system_data) {}

template <typename LoggerInterface>
void LoggerFactoryImpl::BindNewLogger(
    std::unique_ptr<logger::ProjectContext> project_context,
    fidl::InterfaceRequest<LoggerInterface> request,
    fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>* binding_set) {
  std::unique_ptr<logger::Logger> logger;
  if (undated_event_manager_.expired()) {
    logger = std::make_unique<logger::Logger>(std::move(project_context), logger_encoder_,
                                              event_aggregator_, observation_writer_, system_data_,
                                              internal_logger_);
  } else {
    logger = std::make_unique<logger::Logger>(
        std::move(project_context), logger_encoder_, event_aggregator_, observation_writer_,
        system_data_, validated_clock_, undated_event_manager_, internal_logger_);
  }
  binding_set->AddBinding(std::make_unique<LoggerImpl>(std::move(logger), timer_manager_),
                          std::move(request));
}

template <typename LoggerInterface, typename Callback>
void LoggerFactoryImpl::CreateAndBindLoggerFromProjectId(
    uint32_t project_id, fidl::InterfaceRequest<LoggerInterface> request, Callback callback,
    fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>* binding_set) {
  auto project_context =
      global_project_context_factory_->NewProjectContext(kFuchsiaCustomerId, project_id);
  if (!project_context) {
    FX_LOGS(ERROR) << "The CobaltRegistry bundled with this release does not "
                      "include a Fuchsia project with ID "
                   << project_id;
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  BindNewLogger(std::move(project_context), std::move(request), binding_set);
  callback(Status::OK);
}

void LoggerFactoryImpl::CreateLoggerFromProjectId(
    uint32_t project_id, fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
    CreateLoggerFromProjectIdCallback callback) {
  CreateAndBindLoggerFromProjectId(project_id, std::move(request), std::move(callback),
                                   &logger_bindings_);
}

void LoggerFactoryImpl::CreateLoggerSimpleFromProjectId(
    uint32_t project_id, fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
    CreateLoggerSimpleFromProjectIdCallback callback) {
  CreateAndBindLoggerFromProjectId(project_id, std::move(request), std::move(callback),
                                   &logger_simple_bindings_);
}

}  // namespace cobalt
