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
#include "src/lib/fxl/macros.h"
#include "third_party/cobalt/src/public/cobalt_service_interface.h"

namespace cobalt {

class LoggerFactoryImpl : public fuchsia::cobalt::LoggerFactory {
 public:
  LoggerFactoryImpl(TimerManager* timer_manager, CobaltServiceInterface* cobalt_service);

  void ShutDown();

 private:
  // Extracts the Cobalt 1.0 project with the given |project_id| from the global CobaltRegistry, if
  // there is such a project in the registry, and uses this to construct a LoggerImpl. Binds this to
  // |request| and stores the binding in |binding_set|. |callback| will be invoked with OK upon
  // success or an error status otherwise. |LoggerInterface| should be Logger or LoggerSimple.
  template <typename LoggerInterface, typename Callback>
  void CreateAndBindLoggerFromProjectId(
      uint32_t customer_id, uint32_t project_id, fidl::InterfaceRequest<LoggerInterface> request,
      Callback callback,
      fidl::BindingSet<LoggerInterface, std::unique_ptr<LoggerInterface>>* binding_set);

  void CreateLoggerFromProjectId(uint32_t project_id,
                                 fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
                                 CreateLoggerFromProjectIdCallback callback);

  void CreateLoggerSimpleFromProjectId(
      uint32_t project_id, fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> request,
      CreateLoggerSimpleFromProjectIdCallback callback);

  void CreateLoggerFromProjectSpec(uint32_t customer_id, uint32_t project_id,
                                   fidl::InterfaceRequest<fuchsia::cobalt::Logger> request,
                                   CreateLoggerFromProjectSpecCallback callback);

  fidl::BindingSet<fuchsia::cobalt::Logger, std::unique_ptr<fuchsia::cobalt::Logger>>
      logger_bindings_;
  fidl::BindingSet<fuchsia::cobalt::LoggerSimple, std::unique_ptr<fuchsia::cobalt::LoggerSimple>>
      logger_simple_bindings_;

  bool shut_down_ = false;
  TimerManager* timer_manager_;             // not owned
  CobaltServiceInterface* cobalt_service_;  // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(LoggerFactoryImpl);
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_LOGGER_FACTORY_IMPL_H_
