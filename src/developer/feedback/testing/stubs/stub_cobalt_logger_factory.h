// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_STUB_COBALT_LOGGER_FACTORY_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_STUB_COBALT_LOGGER_FACTORY_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>

#include <memory>

#include "src/developer/feedback/testing/stubs/stub_cobalt_logger.h"
#include "src/developer/feedback/utils/cobalt_event.h"
#include "src/lib/fxl/logging.h"

namespace feedback {

// Defines the interface all stub logger factories must implement and provides common functionality.
class StubCobaltLoggerFactoryBase : public fuchsia::cobalt::LoggerFactory {
 public:
  StubCobaltLoggerFactoryBase(std::unique_ptr<StubCobaltLoggerBase> logger)
      : logger_(std::move(logger)) {}
  virtual ~StubCobaltLoggerFactoryBase() {}

  // Returns a request handler for binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::cobalt::LoggerFactory> GetHandler() {
    return factory_bindings_.GetHandler(this);
  }

  const CobaltEvent& LastEvent() const { return logger_->LastEvent(); }
  const std::vector<CobaltEvent>& Events() const { return logger_->Events(); }

  bool WasLogEventCalled() const { return logger_->WasLogEventCalled(); }
  bool WasLogEventCountCalled() const { return logger_->WasLogEventCountCalled(); }
  bool WasLogElapsedTimeCalled() const { return logger_->WasLogElapsedTimeCalled(); }
  bool WasLogFrameRateCalled() const { return logger_->WasLogFrameRateCalled(); }
  bool WasLogMemoryUsageCalled() const { return logger_->WasLogMemoryUsageCalled(); }
  bool WasStartTimerCalled() const { return logger_->WasStartTimerCalled(); }
  bool WasEndTimerCalled() const { return logger_->WasEndTimerCalled(); }
  bool WasLogIntHistogramCalled() const { return logger_->WasLogIntHistogramCalled(); }
  bool WasLogCustomEventCalled() const { return logger_->WasLogCustomEventCalled(); }
  bool WasLogCobaltEventCalled() const { return logger_->WasLogCobaltEventCalled(); }
  bool WasLogCobaltEventsCalled() const { return logger_->WasLogCobaltEventsCalled(); }

  void CloseFactoryConnection() { factory_bindings_.CloseAll(); }
  void CloseLoggerConnection() { logger_bindings_.CloseAll(); }
  void CloseAllConnections() {
    CloseFactoryConnection();
    CloseLoggerConnection();
  }

 protected:
  virtual void CreateLoggerFromProjectName(
      std::string project_name, fuchsia::cobalt::ReleaseStage release_stage,
      fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger,
      fuchsia::cobalt::LoggerFactory::CreateLoggerFromProjectNameCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }
  virtual void CreateLogger(
      fuchsia::cobalt::ProjectProfile profile,
      ::fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger,
      fuchsia::cobalt::LoggerFactory::CreateLoggerCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }
  virtual void CreateLoggerSimple(
      fuchsia::cobalt::ProjectProfile profile,
      fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> logger,
      fuchsia::cobalt::LoggerFactory::CreateLoggerSimpleCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }
  virtual void CreateLoggerSimpleFromProjectName(
      std::string project_name, fuchsia::cobalt::ReleaseStage release_stage,
      fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> logger,
      fuchsia::cobalt::LoggerFactory::CreateLoggerSimpleFromProjectNameCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }
  void CreateLoggerFromProjectId(
      uint32_t project_id, ::fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger,
      fuchsia::cobalt::LoggerFactory::CreateLoggerFromProjectIdCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }
  void CreateLoggerSimpleFromProjectId(
      uint32_t project_id, ::fidl::InterfaceRequest<fuchsia::cobalt::LoggerSimple> logger,
      fuchsia::cobalt::LoggerFactory::CreateLoggerSimpleFromProjectIdCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  std::unique_ptr<StubCobaltLoggerBase> logger_;
  fidl::BindingSet<fuchsia::cobalt::Logger> logger_bindings_;
  fidl::BindingSet<fuchsia::cobalt::LoggerFactory> factory_bindings_;
};

// Always succeed in setting up the logger.
class StubCobaltLoggerFactory : public StubCobaltLoggerFactoryBase {
 public:
  StubCobaltLoggerFactory(
      std::unique_ptr<StubCobaltLoggerBase> logger = std::make_unique<StubCobaltLogger>())
      : StubCobaltLoggerFactoryBase(std::move(logger)) {}

 private:
  void CreateLoggerFromProjectId(
      uint32_t project_id, fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger,
      LoggerFactory::CreateLoggerFromProjectIdCallback callback) override;
};

// Always close the connection before setting up the logger.
class StubCobaltLoggerFactoryClosesConnection : public StubCobaltLoggerFactoryBase {
 public:
  StubCobaltLoggerFactoryClosesConnection()
      : StubCobaltLoggerFactoryBase(std::make_unique<StubCobaltLoggerBase>()) {}

 private:
  void CreateLoggerFromProjectId(
      uint32_t project_id, fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger,
      LoggerFactory::CreateLoggerFromProjectIdCallback callback) override;
};

// Always fail to create the logger.
class StubCobaltLoggerFactoryFailsToCreateLogger : public StubCobaltLoggerFactoryBase {
 public:
  StubCobaltLoggerFactoryFailsToCreateLogger()
      : StubCobaltLoggerFactoryBase(std::make_unique<StubCobaltLoggerBase>()) {}

 private:
  void CreateLoggerFromProjectId(
      uint32_t project_id, fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger,
      LoggerFactory::CreateLoggerFromProjectIdCallback callback) override;
};

// Delay calling the callee provided callback by the specified delay.
class StubCobaltLoggerFactoryDelaysCallback : public StubCobaltLoggerFactoryBase {
 public:
  StubCobaltLoggerFactoryDelaysCallback(std::unique_ptr<StubCobaltLoggerBase> logger,
                                        async_dispatcher_t* dispatcher, zx::duration delay)
      : StubCobaltLoggerFactoryBase(std::move(logger)), dispatcher_(dispatcher), delay_(delay) {}

 private:
  void CreateLoggerFromProjectId(
      uint32_t project_id, fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger,
      fuchsia::cobalt::LoggerFactory::CreateLoggerFromProjectIdCallback callback) override;

  async_dispatcher_t* dispatcher_;
  zx::duration delay_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_STUB_COBALT_LOGGER_FACTORY_H_
