// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory.h>

#include <zxtest/base/assertion.h>
#include <zxtest/base/runner.h>
#include <zxtest/base/test-driver.h>
#include <zxtest/base/test.h>
#include <zxtest/c/zxtest.h>

namespace {

class CTestWrapper : public zxtest::Test {
public:
    CTestWrapper() = default;
    ~CTestWrapper() final {}

    void SetCFunction(zxtest_test_fn_t test_fn) {
        ZX_ASSERT_MSG(test_fn_ == nullptr, "Once set test_fn_ should never change.");
        test_fn_ = test_fn;
    }

private:
    void TestBody() final { test_fn_(); }

    zxtest_test_fn_t test_fn_ = nullptr;
};

} // namespace

int zxtest_run_all_tests(int argc, char** argv) {
    return zxtest::RunAllTests(argc, argv);
}

zxtest_test_ref_t zxtest_runner_register_test(const char* testcase_name, const char* test_name,
                                              const char* file, int line_number,
                                              zxtest_test_fn_t test_fn) {
    zxtest::TestRef test_ref =
        zxtest::Runner::GetInstance()->RegisterTest<zxtest::Test, CTestWrapper>(
            testcase_name, test_name, file, line_number,
            [test_fn](zxtest::internal::TestDriver* driver) -> std::unique_ptr<zxtest::Test> {
                std::unique_ptr<CTestWrapper> test_wrapper =
                    zxtest::Test::Create<CTestWrapper>(driver);
                test_wrapper->SetCFunction(test_fn);
                return test_wrapper;
            });

    return {.test_index = test_ref.test_index, .test_case_index = test_ref.test_case_index};
}

void zxtest_runner_notify_assertion(const char* desc, const char* expected,
                                    const char* expected_eval, const char* actual,
                                    const char* actual_eval, const char* file, int64_t line,
                                    bool is_fatal) {
    zxtest::Runner::GetInstance()->NotifyAssertion(
        zxtest::Assertion(desc, expected, expected_eval, actual, actual_eval,
                          {.filename = file, .line_number = line}, is_fatal));
}

bool zxtest_runner_should_abort_current_test(void) {
    return zxtest::Runner::GetInstance()->ShouldAbortCurrentTest();
}
