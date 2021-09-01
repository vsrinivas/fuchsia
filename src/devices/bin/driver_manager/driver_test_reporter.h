// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_TEST_REPORTER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_TEST_REPORTER_H_

#include <fidl/fuchsia.driver.test/cpp/wire.h>

#include <fbl/string.h>

class DriverTestReporter : public fidl::WireServer<fuchsia_driver_test::Logger> {
 public:
  explicit DriverTestReporter(const fbl::String& driver_name) : driver_name_(driver_name) {}
  virtual ~DriverTestReporter() = default;

  // Implements fuchsia.driver.test.Logger.
  void LogMessage(LogMessageRequestView request, LogMessageCompleter::Sync& completer) override;
  void LogTestCase(LogTestCaseRequestView request, LogTestCaseCompleter::Sync& completer) override;

  virtual void TestStart();
  virtual void TestFinished();

 private:
  const fbl::String driver_name_;
  uint64_t total_cases_ = 0;
  uint64_t total_passed_ = 0;
  uint64_t total_failed_ = 0;
  uint64_t total_skipped_ = 0;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_TEST_REPORTER_H_
