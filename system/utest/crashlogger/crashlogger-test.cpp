// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <regex.h>
#include <unistd.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <fbl/algorithm.h>
#include <fbl/unique_ptr.h>
#include <launchpad/launchpad.h>
#include <unittest/unittest.h>

static const char* g_executable_filename;

// This should match the value used by crashlogger.
static const uint64_t kSysExceptionKey = 1166444u;

// Helper class for using POSIX regular expressions.
class RegEx {
public:
    RegEx(const char* regex_str) {
        int err = regcomp(&regex_, regex_str, REG_EXTENDED);
        if (err != 0) {
            char msg[100];
            msg[0] = '\0';
            regerror(err, &regex_, msg, sizeof(msg));
            fprintf(stderr, "Regex compilation failed: %s\n", msg);
            abort();
        }
    }

    ~RegEx() {
        regfree(&regex_);
    }

    bool Matches(const char* str) {
        return regexec(&regex_, str, 0, NULL, 0) == 0;
    }

private:
    regex_t regex_;
};

_Pragma("GCC diagnostic push")
// Tell GCC not to warn about "-Winfinite-recursion" being unknown.
_Pragma("GCC diagnostic ignored \"-Wpragmas\"")
// Tell Clang not to warn about recursion in the following function.
_Pragma("GCC diagnostic ignored \"-Winfinite-recursion\"")

void stack_overflow(volatile int* ptr) {
    // To stop the compiler from making the function call a tail call,
    // allocate a variable on the stack and take its address.  To stop the
    // compiler optimizing the variable away, do a volatile write to it.
    if (ptr)
        *ptr = 0;
    volatile int x;
    stack_overflow(&x);
}

_Pragma("GCC diagnostic pop")

void handle_crash_arg(int argc, char** argv) {
    if (argc >= 2 && !strcmp(argv[1], "--crash")) {
        if (argc == 3 && !strcmp(argv[2], "write_to_zero")) {
            *(volatile int*) 0 = 0x12345678;
            exit(1);
        }
        if (argc == 3 && !strcmp(argv[2], "stack_overflow")) {
            stack_overflow(nullptr);
            exit(1);
        }
        fprintf(stderr, "Unrecognized arguments");
        exit(1);
    }
}

// This tests the output of crashlogger given a process that crashes.  It
// launches a test instance of crashlogger in order to capture its output.
bool test_crash(const char* crash_arg) {
    const char* argv[] = { g_executable_filename, "--crash", crash_arg };
    launchpad_t* crasher_lp;
    launchpad_create(0, "crash-test", &crasher_lp);

    // Make sure we bind an exception port to the process before we start
    // it running.
    mx_handle_t crasher_proc = launchpad_get_process_handle(crasher_lp);
    mx_handle_t exception_port;
    ASSERT_EQ(mx_port_create(0, &exception_port), MX_OK);
    ASSERT_EQ(mx_task_bind_exception_port(crasher_proc, exception_port,
                                          kSysExceptionKey, 0), MX_OK);

    // Launch the crasher process.
    launchpad_load_from_file(crasher_lp, argv[0]);
    launchpad_clone(crasher_lp, LP_CLONE_ALL);
    launchpad_set_args(crasher_lp, fbl::count_of(argv), argv);
    const char* errmsg;
    ASSERT_EQ(launchpad_go(crasher_lp, &crasher_proc, &errmsg), MX_OK);

    // Launch a test instance of crashlogger.
    const char* crashlogger_argv[] = { "/boot/bin/crashlogger" };
    launchpad_t* crashlogger_lp;
    launchpad_create(0, "crashlogger-test-instance", &crashlogger_lp);
    launchpad_load_from_file(crashlogger_lp, crashlogger_argv[0]);
    launchpad_clone(crasher_lp, LP_CLONE_ALL);
    launchpad_set_args(crashlogger_lp, fbl::count_of(crashlogger_argv),
                       crashlogger_argv);
    mx_handle_t handles[] = { exception_port };
    uint32_t handle_types[] = { PA_HND(PA_USER0, 0) };
    launchpad_add_handles(crashlogger_lp, fbl::count_of(handles), handles,
                          handle_types);
    int pipe_fd;
    launchpad_add_pipe(crashlogger_lp, &pipe_fd, STDOUT_FILENO);
    mx_handle_t crashlogger_proc;
    ASSERT_EQ(launchpad_go(crashlogger_lp, &crashlogger_proc, &errmsg), MX_OK);

    // Read crashlogger's output into a buffer.  Stop reading when we get
    // an end-of-backtrace line which matches the following regular
    // expression.
    RegEx end_regex("^bt#\\d+: end");
    FILE* fp = fdopen(pipe_fd, "r");
    uint32_t output_size = 10000;
    uint32_t size_read = 0;
    fbl::unique_ptr<char[]> output(new char[output_size]);
    for (;;) {
        char* line_buf = &output[size_read];
        int32_t size_remaining = output_size - size_read;
        ASSERT_GT(size_remaining, 1);
        if (!fgets(line_buf, size_remaining, fp))
            break;
        size_read += (uint32_t)strlen(line_buf);
        if (end_regex.Matches(line_buf))
            break;
    }
    fclose(fp);

    // Check that the output contains backtrace info.
    RegEx overall_regex(
        "arch: .*\n"
        "(dso: id=.* base=.* name=.*\n)+"
        "(bt#\\d+: pc 0x.* sp 0x.* \\(.*,0x.*\\))+");
    ASSERT_TRUE(overall_regex.Matches(output.get()));

    // Clean up.
    ASSERT_EQ(mx_object_wait_one(crasher_proc, MX_PROCESS_TERMINATED,
                                 MX_TIME_INFINITE, NULL), MX_OK);
    ASSERT_EQ(mx_handle_close(crasher_proc), MX_OK);
    ASSERT_EQ(mx_task_kill(crashlogger_proc), MX_OK);
    ASSERT_EQ(mx_object_wait_one(crashlogger_proc, MX_PROCESS_TERMINATED,
                                 MX_TIME_INFINITE, NULL), MX_OK);
    ASSERT_EQ(mx_handle_close(crashlogger_proc), MX_OK);
    return true;
}

bool test_crash_write0() {
    BEGIN_TEST;
    test_crash("write_to_zero");
    END_TEST;
}

bool test_crash_stack_overflow() {
    BEGIN_TEST;
    test_crash("stack_overflow");
    END_TEST;
}

BEGIN_TEST_CASE(crashlogger_tests)
RUN_TEST(test_crash_write0)
RUN_TEST(test_crash_stack_overflow)
END_TEST_CASE(crashlogger_tests)

int main(int argc, char** argv) {
    g_executable_filename = argv[0];
    handle_crash_arg(argc, argv);

    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
