// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_DEVCOORDINATOR_DRIVER_TEST_REPORTER_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_DEVCOORDINATOR_DRIVER_TEST_REPORTER_H_

#include <fbl/string.h>
#include <fuchsia/driver/test/c/fidl.h>

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

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_DEVCOORDINATOR_DRIVER_TEST_REPORTER_H_
