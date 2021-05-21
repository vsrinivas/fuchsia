// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_COBALT_LOGGER_FACTORY_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_COBALT_LOGGER_FACTORY_H_

#include <fuchsia/metrics/cpp/fidl.h>
#include <fuchsia/metrics/cpp/fidl_test_base.h>

#include <limits>
#include <memory>

#include "src/developer/forensics/testing/stubs/cobalt_logger.h"
#include "src/developer/forensics/testing/stubs/fidl_server.h"
#include "src/developer/forensics/utils/cobalt/event.h"

namespace forensics {
namespace stubs {

// Defines the interface all stub logger factories must implement and provides common functionality.
class CobaltLoggerFactoryBase
    : public SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::metrics, MetricEventLoggerFactory) {
 public:
  CobaltLoggerFactoryBase(std::unique_ptr<CobaltLoggerBase> logger) : logger_(std::move(logger)) {}
  virtual ~CobaltLoggerFactoryBase() {}

  const cobalt::Event& LastEvent() const { return logger_->LastEvent(); }
  const std::vector<cobalt::Event>& Events() const { return logger_->Events(); }

  bool WasMethodCalled(cobalt::EventType name) const { return logger_->WasMethodCalled(name); }

  void CloseLoggerConnection();

 protected:
  std::unique_ptr<CobaltLoggerBase> logger_;
  std::unique_ptr<::fidl::Binding<fuchsia::metrics::MetricEventLogger>> logger_binding_;
};

// Always succeed in setting up the logger.
class CobaltLoggerFactory : public CobaltLoggerFactoryBase {
 public:
  CobaltLoggerFactory(std::unique_ptr<CobaltLoggerBase> logger = std::make_unique<CobaltLogger>())
      : CobaltLoggerFactoryBase(std::move(logger)) {}

 private:
  // |fuchsia::metrics::MetricEventLoggerFactory|
  void CreateMetricEventLogger(
      ::fuchsia::metrics::ProjectSpec project_spec,
      ::fidl::InterfaceRequest<::fuchsia::metrics::MetricEventLogger> logger,
      CreateMetricEventLoggerCallback callback) override;
};

// Always close the connection before setting up the logger.
class CobaltLoggerFactoryClosesConnection : public CobaltLoggerFactoryBase {
 public:
  CobaltLoggerFactoryClosesConnection()
      : CobaltLoggerFactoryBase(std::make_unique<CobaltLoggerBase>()) {}

 private:
  // |fuchsia::metrics::MetricEventLoggerFactory|
  void CreateMetricEventLogger(
      ::fuchsia::metrics::ProjectSpec project_spec,
      ::fidl::InterfaceRequest<::fuchsia::metrics::MetricEventLogger> logger,
      CreateMetricEventLoggerCallback callback) override;
};

// Fail to create the logger.
class CobaltLoggerFactoryFailsToCreateLogger : public CobaltLoggerFactoryBase {
 public:
  CobaltLoggerFactoryFailsToCreateLogger()
      : CobaltLoggerFactoryBase(std::make_unique<CobaltLoggerBase>()) {}

 private:
  // |fuchsia::metrics::MetricEventLoggerFactory|
  void CreateMetricEventLogger(
      ::fuchsia::metrics::ProjectSpec project_spec,
      ::fidl::InterfaceRequest<::fuchsia::metrics::MetricEventLogger> logger,
      CreateMetricEventLoggerCallback callback) override;
};

// Fail to create the logger until |succeed_after_| attempts have been made.
class CobaltLoggerFactoryCreatesOnRetry : public CobaltLoggerFactoryBase {
 public:
  CobaltLoggerFactoryCreatesOnRetry(uint64_t succeed_after)
      : CobaltLoggerFactoryBase(std::make_unique<CobaltLogger>()), succeed_after_(succeed_after) {}

 private:
  // |fuchsia::metrics::MetricEventLoggerFactory|
  void CreateMetricEventLogger(
      ::fuchsia::metrics::ProjectSpec project_spec,
      ::fidl::InterfaceRequest<::fuchsia::metrics::MetricEventLogger> logger,
      CreateMetricEventLoggerCallback callback) override;

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
  // |fuchsia::metrics::MetricEventLoggerFactory|
  void CreateMetricEventLogger(
      ::fuchsia::metrics::ProjectSpec project_spec,
      ::fidl::InterfaceRequest<::fuchsia::metrics::MetricEventLogger> logger,
      CreateMetricEventLoggerCallback callback) override;

  async_dispatcher_t* dispatcher_;
  zx::duration delay_;
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_COBALT_LOGGER_FACTORY_H_
