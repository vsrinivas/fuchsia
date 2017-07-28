// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <regex.h>
#include <unistd.h>

#include <magenta/processargs.h>
#include <mxtl/unique_ptr.h>
#include <launchpad/launchpad.h>
#include <unittest/unittest.h>

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

// This tests the output of crashlogger given a process that crashes.  It
// launches a test instance of crashlogger in order to capture its output.
bool test_crash(const char* crasher_arg) {
    const char* argv[] = { "/boot/bin/crasher", crasher_arg };
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
    launchpad_set_args(crasher_lp, countof(argv), argv);
    const char* errmsg;
    ASSERT_EQ(launchpad_go(crasher_lp, &crasher_proc, &errmsg), MX_OK);

    // Launch a test instance of crashlogger.
    const char* crashlogger_argv[] = { "/boot/bin/crashlogger" };
    launchpad_t* crashlogger_lp;
    launchpad_create(0, "crashlogger-test-instance", &crashlogger_lp);
    launchpad_load_from_file(crashlogger_lp, crashlogger_argv[0]);
    launchpad_clone(crasher_lp, LP_CLONE_ALL);
    launchpad_set_args(crashlogger_lp, countof(crashlogger_argv),
                       crashlogger_argv);
    mx_handle_t handles[] = { exception_port };
    uint32_t handle_types[] = { PA_HND(PA_USER0, 0) };
    launchpad_add_handles(crashlogger_lp, countof(handles), handles,
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
    mxtl::unique_ptr<char[]> output(new char[output_size]);
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
    test_crash("write0");
    END_TEST;
}

bool test_crash_stack_overflow() {
    BEGIN_TEST;
    test_crash("stackov");
    END_TEST;
}

BEGIN_TEST_CASE(crashlogger_tests)
RUN_TEST(test_crash_write0)
RUN_TEST(test_crash_stack_overflow)
END_TEST_CASE(crashlogger_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
