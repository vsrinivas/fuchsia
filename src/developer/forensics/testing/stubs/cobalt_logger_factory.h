// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_COBALT_LOGGER_FACTORY_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_COBALT_LOGGER_FACTORY_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/cobalt/cpp/fidl_test_base.h>

#include <limits>
#include <memory>

#include "src/developer/forensics/testing/stubs/cobalt_logger.h"
#include "src/developer/forensics/testing/stubs/fidl_server.h"
#include "src/developer/forensics/utils/cobalt/event.h"

namespace forensics {
namespace stubs {

// Defines the interface all stub logger factories must implement and provides common functionality.
class CobaltLoggerFactoryBase
    : public SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::cobalt, LoggerFactory) {
 public:
  CobaltLoggerFactoryBase(std::unique_ptr<CobaltLoggerBase> logger) : logger_(std::move(logger)) {}
  virtual ~CobaltLoggerFactoryBase() {}

  const cobalt::Event& LastEvent() const { return logger_->LastEvent(); }
  const std::vector<cobalt::Event>& Events() const { return logger_->Events(); }

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

  void CloseLoggerConnection();

 protected:
  std::unique_ptr<CobaltLoggerBase> logger_;
  std::unique_ptr<::fidl::Binding<fuchsia::cobalt::Logger>> logger_binding_;
};

// Always succeed in setting up the logger.
class CobaltLoggerFactory : public CobaltLoggerFactoryBase {
 public:
  CobaltLoggerFactory(std::unique_ptr<CobaltLoggerBase> logger = std::make_unique<CobaltLogger>())
      : CobaltLoggerFactoryBase(std::move(logger)) {}

 private:
  // |fuchsia::cobalt::LoggerFactory|
  void CreateLoggerFromProjectId(
      uint32_t project_id, ::fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger,
      LoggerFactory::CreateLoggerFromProjectIdCallback callback) override;
};

// Always close the connection before setting up the logger.
class CobaltLoggerFactoryClosesConnection : public CobaltLoggerFactoryBase {
 public:
  CobaltLoggerFactoryClosesConnection()
      : CobaltLoggerFactoryBase(std::make_unique<CobaltLoggerBase>()) {}

 private:
  // |fuchsia::cobalt::LoggerFactory|
  STUB_METHOD_CLOSES_CONNECTION(CreateLoggerFromProjectId, uint32_t,
                                ::fidl::InterfaceRequest<fuchsia::cobalt::Logger>,
                                LoggerFactory::CreateLoggerFromProjectIdCallback);
};

// Fail to create the logger.
class CobaltLoggerFactoryFailsToCreateLogger : public CobaltLoggerFactoryBase {
 public:
  CobaltLoggerFactoryFailsToCreateLogger()
      : CobaltLoggerFactoryBase(std::make_unique<CobaltLoggerBase>()) {}

 private:
  // |fuchsia::cobalt::LoggerFactory|
  void CreateLoggerFromProjectId(
      uint32_t project_id, ::fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger,
      LoggerFactory::CreateLoggerFromProjectIdCallback callback) override;
};

// Fail to create the logger until |succeed_after_| attempts have been made.
class CobaltLoggerFactoryCreatesOnRetry : public CobaltLoggerFactoryBase {
 public:
  CobaltLoggerFactoryCreatesOnRetry(uint64_t succeed_after)
      : CobaltLoggerFactoryBase(std::make_unique<CobaltLogger>()), succeed_after_(succeed_after) {}

 private:
  // |fuchsia::cobalt::LoggerFactory|
  void CreateLoggerFromProjectId(
      uint32_t project_id, ::fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger,
      LoggerFactory::CreateLoggerFromProjectIdCallback callback) override;

  const uint64_t succeed_after_;
  uint64_t num_calls_ = 0;
};

// Delay calling the callee provided callback by the specified delay.
class CobaltLoggerFactoryDelaysCallback : public CobaltLoggerFactoryBase {
 public:
  CobaltLoggerFactoryDelaysCallback(std::unique_ptr<CobaltLoggerBase> logger,
                                    async_dispatcher_t* dispatcher, zx::duration delay)
      : CobaltLoggerFactoryBase(std::move(logger)), dispatcher_(dispatcher), delay_(delay) {}

 private:
  // |fuchsia::cobalt::LoggerFactory|
  void CreateLoggerFromProjectId(
      uint32_t project_id, ::fidl::InterfaceRequest<fuchsia::cobalt::Logger> logger,
      fuchsia::cobalt::LoggerFactory::CreateLoggerFromProjectIdCallback callback) override;

  async_dispatcher_t* dispatcher_;
  zx::duration delay_;
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_COBALT_LOGGER_FACTORY_H_
