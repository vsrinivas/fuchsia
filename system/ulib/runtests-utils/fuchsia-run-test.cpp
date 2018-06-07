// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtests-utils/fuchsia-run-test.h>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <fbl/auto_call.h>
#include <launchpad/launchpad.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

namespace runtests {

Result FuchsiaRunTest(const char* argv[], int argc,
                      const char* output_filename) {
    int fds[2];
    const char* path = argv[0];

    launchpad_t* lp = nullptr;
    zx_status_t status = ZX_OK;

    zx::job test_job;
    status = zx::job::create(zx_job_default(), 0, &test_job);
    if (status != ZX_OK) {
        printf("FAILURE: zx::job::create() returned %d\n", status);
        return Result(path, FAILED_TO_LAUNCH, 0);
    }
    auto auto_call_kill_job =
        fbl::MakeAutoCall([&test_job]() { test_job.kill(); });
    auto auto_call_launchpad_destroy = fbl::MakeAutoCall([&lp]() {
        if (lp) {
            launchpad_destroy(lp);
        }
    });
    status = test_job.set_property(ZX_PROP_NAME, "run-test", sizeof("run-test"));
    if (status != ZX_OK) {
        printf("FAILURE: set_property() returned %d\n", status);
        return Result(path, FAILED_TO_LAUNCH, 0);
    }
    status = launchpad_create(test_job.get(), path, &lp);
    if (status != ZX_OK) {
        printf("FAILURE: launchpad_create() returned %d\n", status);
        return Result(path, FAILED_TO_LAUNCH, 0);
    }
    status = launchpad_load_from_file(lp, path);
    if (status != ZX_OK) {
        printf("FAILURE: launchpad_load_from_file() returned %d\n", status);
        return Result(path, FAILED_TO_LAUNCH, 0);
    }
    status = launchpad_clone(lp, LP_CLONE_FDIO_ALL | LP_CLONE_ENVIRON);
    if (status != ZX_OK) {
        printf("FAILURE: launchpad_clone() returned %d\n", status);
        return Result(path, FAILED_TO_LAUNCH, 0);
    }
    if (output_filename != nullptr) {
        if (pipe(fds)) {
            printf("FAILURE: Failed to create pipe: %s\n", strerror(errno));
            return Result(path, FAILED_TO_LAUNCH, 0);
        }
        status = launchpad_clone_fd(lp, fds[1], STDOUT_FILENO);
        if (status != ZX_OK) {
            printf("FAILURE: launchpad_clone_fd() returned %d\n", status);
            return Result(path, FAILED_TO_LAUNCH, 0);
        }
        status = launchpad_transfer_fd(lp, fds[1], STDERR_FILENO);
        if (status != ZX_OK) {
            printf("FAILURE: launchpad_transfer_fd() returned %d\n", status);
            return Result(path, FAILED_TO_LAUNCH, 0);
        }
    }
    launchpad_set_args(lp, argc, argv);
    const char* errmsg;
    zx::process process;
    status = launchpad_go(lp, process.reset_and_get_address(), &errmsg);
    lp = nullptr; // launchpad_go destroys lp, null it so we don't try to destroy
                  // again.
    if (status != ZX_OK) {
        printf("FAILURE: Failed to launch %s: %d: %s\n", path, status, errmsg);
        return Result(path, FAILED_TO_LAUNCH, 0);
    }
    // Tee output.
    if (output_filename != nullptr) {
        FILE* output_file = fopen(output_filename, "w");
        if (output_file == nullptr) {
            printf("FAILURE: Could not open output file at %s: %s\n", output_filename,
                   strerror(errno));
            return Result(path, FAILED_DURING_IO, 0);
        }
        char buf[1024];
        ssize_t bytes_read = 0;
        while ((bytes_read = read(fds[0], buf, sizeof(buf))) > 0) {
            fwrite(buf, 1, bytes_read, output_file);
            fwrite(buf, 1, bytes_read, stdout);
        }
        if (fclose(output_file)) {
            printf("FAILURE:  Could not close %s: %s", output_filename,
                   strerror(errno));
            return Result(path, FAILED_DURING_IO, 0);
        }
    }
    status =
        process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
    if (status != ZX_OK) {
        printf("FAILURE: Failed to wait for process exiting %s: %d\n", path,
               status);
        return Result(path, FAILED_TO_WAIT, 0);
    }

    // read the return code
    zx_info_process_t proc_info;
    status = process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info),
                              nullptr, nullptr);

    if (status != ZX_OK) {
        printf("FAILURE: Failed to get process return code %s: %d\n", path, status);
        return Result(path, FAILED_TO_RETURN_CODE, 0);
    }

    if (proc_info.return_code != 0) {
        printf("FAILURE: %s exited with nonzero status: %" PRId64 "\n", path,
               proc_info.return_code);
        return Result(path, FAILED_NONZERO_RETURN_CODE, proc_info.return_code);
    }

    printf("PASSED: %s passed\n", path);
    return Result(path, SUCCESS, 0);
}

} // namespace runtests
