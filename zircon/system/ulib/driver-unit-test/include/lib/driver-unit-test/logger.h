// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <utility>

#include <fbl/string.h>
#include <fuchsia/driver/test/c/fidl.h>
#include <lib/zx/channel.h>
#include <zxtest/base/observer.h>

namespace driver_unit_test {

// Logger is used to output test events and messages to a specified channel.
//
// Tests can use RunZxTests to set up the logger instance, and in the tests retrieve
// the instance via |GetInstance|. Drivers can then log custom messages using |SendLogMessage|.
class Logger : public zxtest::LifecycleObserver {
public:
    // Populates |instance_| with a new logger instance.
    static zx_status_t CreateInstance(zx::unowned_channel ch);
    static Logger* GetInstance() { return instance_.get(); }
    static void DeleteInstance() { instance_ = nullptr; }

    // Sends a log message to the channel.
    static zx_status_t SendLogMessage(const char* msg);

    // LifecycleObserver methods.
    void OnTestCaseStart(const zxtest::TestCase& test_case);
    void OnTestCaseEnd(const zxtest::TestCase& test_case);
    void OnTestSuccess(const zxtest::TestCase& test_case, const zxtest::TestInfo& test);
    void OnTestFailure(const zxtest::TestCase& test_case, const zxtest::TestInfo& test);
    void OnTestSkip(const zxtest::TestCase& test_case, const zxtest::TestInfo& test);

private:
    explicit Logger(zx::unowned_channel ch) : channel_(ch) {}

    // Sends the test case result to the channel.
    zx_status_t SendLogTestCase();

    // This is static so that the instance is accessible to the test cases.
    static std::unique_ptr<Logger> instance_;

    // The channel to send FIDL messages to.
    zx::unowned_channel channel_;

    // Current test case information.
    fbl::String test_case_name_;
    fuchsia_driver_test_TestCaseResult test_case_result_;
};

}  // namespace driver_unit_test
