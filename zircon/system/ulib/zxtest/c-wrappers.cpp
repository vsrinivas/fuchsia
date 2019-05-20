// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cinttypes>
#include <memory.h>

#include <zxtest/base/assertion.h>
#include <zxtest/base/runner.h>
#include <zxtest/base/test-driver.h>
#include <zxtest/base/test.h>
#include <zxtest/c/zxtest.h>

#ifdef __Fuchsia__
#include <zxtest/base/death-statement.h>
#endif

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

bool zxtest_runner_current_test_has_fatal_failures(void) {
    return zxtest::Runner::GetInstance()->CurrentTestHasFatalFailures();
}

bool zxtest_runner_current_test_has_failures(void) {
    return zxtest::Runner::GetInstance()->CurrentTestHasFailures();
}

size_t _zxtest_print_int32(int32_t val, char* buffer, size_t buffer_size) {
    return snprintf(buffer, buffer_size, "%" PRIi32, val);
}

size_t _zxtest_print_uint32(uint32_t val, char* buffer, size_t buffer_size) {
    return snprintf(buffer, buffer_size, "%" PRIu32, val);
}

size_t _zxtest_print_int64(int64_t val, char* buffer, size_t buffer_size) {
    return snprintf(buffer, buffer_size, "%" PRIi64, val);
}

size_t _zxtest_print_uint64(uint64_t val, char* buffer, size_t buffer_size) {
    return snprintf(buffer, buffer_size, "%" PRIu64, val);
}

size_t _zxtest_print_bool(bool val, char* buffer, size_t buffer_size) {
    return snprintf(buffer, buffer_size, "%s", (val) ? "true" : "false");
}

size_t _zxtest_print_str(const char* val, char* buffer, size_t buffer_size) {
    return snprintf(buffer, buffer_size, "%s", (val == nullptr) ? "<nullptr>" : val);
}

size_t _zxtest_print_ptr(const void* val, char* buffer, size_t buffer_size) {
    if (val == nullptr) {
        return snprintf(buffer, buffer_size, "<nullptr>");
    }
    return snprintf(buffer, buffer_size, "%p", val);
}

size_t _zxtest_print_hex(const void* val, size_t size, char* buffer, size_t buffer_size) {
    if (buffer_size == 0 || buffer_size < 2 * size + 1) {
        return 3 * size + 1;
    }

    if (val == nullptr) {
        snprintf(buffer, 9, "<nullptr>");
    }

    for (size_t curr = 0; curr < size; ++curr) {
        snprintf(buffer + 3 * curr, buffer_size - curr, "%02X%*s", *((uint8_t*)(val) + curr),
                 (curr < size - 1) ? 1 : 0, " ");
    }
    return 0;
}

void zxtest_c_clean_buffer(char** buffer) {
    free(*buffer);
}

void zxtest_runner_fail_current_test(bool is_fatal, const char* file, int line,
                                     const char* message) {
    zxtest::Runner::GetInstance()->NotifyAssertion(
        zxtest::Assertion(message, {.filename = file, .line_number = line}, is_fatal));
}

#ifdef __Fuchsia__
bool zxtest_death_statement_execute(zxtest_test_fn_t statement, enum DeathResult result,
                                    const char* file, int line, const char* message) {
    zxtest::internal::DeathStatement death_statement([statement]() { statement(); });
    death_statement.Execute();
    bool success = false;

    switch (result) {
    case DeathResult::kDeathResultComplete:
        success = (death_statement.state() == zxtest::internal::DeathStatement::State::kSuccess);
        break;
    case DeathResult::kDeathResultRaiseException:
        success = (death_statement.state() == zxtest::internal::DeathStatement::State::kException);
        break;
    default:
        break;
    }

    if (success) {
        return true;
    }

    if (death_statement.state() == zxtest::internal::DeathStatement::State::kBadState) {
        zxtest::Runner::GetInstance()->NotifyFatalError();
    }

    // For now all death death_statements assertions are fatal.
    std::string error_message;
    if (!death_statement.error_message().empty()) {
        zxtest_runner_fail_current_test(true, file, line, death_statement.error_message().data());
    } else {
        zxtest_runner_fail_current_test(true, file, line, message);
    }
    return false;
}
#endif
