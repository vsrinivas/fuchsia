// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtests-utils/fuchsia-run-test.h>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <zircon/status.h>

namespace runtests {

fbl::unique_ptr<Result> FuchsiaRunTest(const char* argv[],
                                       const char* output_filename) {
    const char* path = argv[0];

    // If |output_filename| is provided, prepare the file descriptors that will
    // be used to tee the stdout/stderr of the test into the associated file.
    fbl::unique_fd fds[2];
    size_t fdio_action_count = 1;  // At least one for SET_NAME.
    if (output_filename != nullptr) {
        int temp_fds[2] = {-1, -1};
        if (pipe(temp_fds)) {
            fprintf(stderr, "FAILURE: Failed to create pipe: %s\n", strerror(errno));
            return fbl::make_unique<Result>(path, FAILED_TO_LAUNCH, 0);
        }
        fds[0].reset(temp_fds[0]);
        fds[1].reset(temp_fds[1]);
        fdio_action_count += 2;  // Plus two for CLONE_FD and TRANSFER_FD.
    }

    // If |output_filename| is not provided, then we will ignore all but the
    // first action.
    const fdio_spawn_action_t fdio_actions[] = {
        {.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = path}},
        {.action = FDIO_SPAWN_ACTION_CLONE_FD,
         .fd = {.local_fd = fds[1].get(), .target_fd = STDOUT_FILENO}},
        {.action = FDIO_SPAWN_ACTION_TRANSFER_FD,
         .fd = {.local_fd = fds[1].get(), .target_fd = STDERR_FILENO}},
    };

    zx_status_t status = ZX_OK;
    zx::job test_job;
    status = zx::job::create(*zx::job::default_job(), 0, &test_job);
    if (status != ZX_OK) {
        fprintf(stderr, "FAILURE: zx::job::create() returned %d\n", status);
        return fbl::make_unique<Result>(path, FAILED_TO_LAUNCH, 0);
    }
    auto auto_call_kill_job =
        fbl::MakeAutoCall([&test_job]() { test_job.kill(); });
    status = test_job.set_property(ZX_PROP_NAME, "run-test", sizeof("run-test"));
    if (status != ZX_OK) {
        fprintf(stderr, "FAILURE: set_property() returned %d\n", status);
        return fbl::make_unique<Result>(path, FAILED_TO_LAUNCH, 0);
    }

    fds[1].release(); // To avoid double close since fdio_spawn_etc() closes it.
    zx::process process;
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    status = fdio_spawn_etc(test_job.get(), FDIO_SPAWN_CLONE_ALL,
                            path, argv, nullptr,
                            fdio_action_count, fdio_actions,
                            process.reset_and_get_address(), err_msg);
    if (status != ZX_OK) {
        fprintf(stderr, "FAILURE: Failed to launch %s: %d (%s): %s\n", path, status,
               zx_status_get_string(status), err_msg);
        return fbl::make_unique<Result>(path, FAILED_TO_LAUNCH, 0);
    }
    // Tee output.
    if (output_filename != nullptr) {
        FILE* output_file = fopen(output_filename, "w");
        if (output_file == nullptr) {
            fprintf(stderr, "FAILURE: Could not open output file at %s: %s\n", output_filename,
                   strerror(errno));
            return fbl::make_unique<Result>(path, FAILED_DURING_IO, 0);
        }
        char buf[1024];
        ssize_t bytes_read = 0;
        while ((bytes_read = read(fds[0].get(), buf, sizeof(buf))) > 0) {
            fwrite(buf, 1, bytes_read, output_file);
            fwrite(buf, 1, bytes_read, stdout);
        }
        if (fclose(output_file)) {
            fprintf(stderr, "FAILURE:  Could not close %s: %s", output_filename,
                   strerror(errno));
            return fbl::make_unique<Result>(path, FAILED_DURING_IO, 0);
        }
    }
    status =
        process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
    if (status != ZX_OK) {
        fprintf(stderr, "FAILURE: Failed to wait for process exiting %s: %d\n", path,
               status);
        return fbl::make_unique<Result>(path, FAILED_TO_WAIT, 0);
    }

    // read the return code
    zx_info_process_t proc_info;
    status = process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info),
                              nullptr, nullptr);

    if (status != ZX_OK) {
        fprintf(stderr, "FAILURE: Failed to get process return code %s: %d\n", path, status);
        return fbl::make_unique<Result>(path, FAILED_TO_RETURN_CODE, 0);
    }

    if (proc_info.return_code != 0) {
        fprintf(stderr, "FAILURE: %s exited with nonzero status: %" PRId64 "\n", path,
               proc_info.return_code);
        return fbl::make_unique<Result>(path, FAILED_NONZERO_RETURN_CODE, proc_info.return_code);
    }

    fprintf(stderr, "PASSED: %s passed\n", path);
    return fbl::make_unique<Result>(path, SUCCESS, 0);
}

} // namespace runtests
