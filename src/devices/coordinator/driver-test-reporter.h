// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_COORDINATOR_DRIVER_TEST_REPORTER_H_
#define SRC_DEVICES_COORDINATOR_DRIVER_TEST_REPORTER_H_

#include <fuchsia/driver/test/c/fidl.h>

#include <fbl/string.h>

namespace devmgr {

class DriverTestReporter {
 public:
  explicit DriverTestReporter(const fbl::String& driver_name) : driver_name_(driver_name) {}
  virtual ~DriverTestReporter() = default;

  // Implements fuchsia.driver.test.Logger.
  virtual void LogMessage(const char* msg, size_t size);
  virtual void LogTestCase(const char* name, size_t name_size,
                           const fuchsia_driver_test_TestCaseResult* result);

  virtual void TestStart();
  virtual void TestFinished();

 private:
  const fbl::String driver_name_;
  uint64_t total_cases_ = 0;
  uint64_t total_passed_ = 0;
  uint64_t total_failed_ = 0;
  uint64_t total_skipped_ = 0;
};

}  // namespace devmgr

#endif  // SRC_DEVICES_COORDINATOR_DRIVER_TEST_REPORTER_H_
