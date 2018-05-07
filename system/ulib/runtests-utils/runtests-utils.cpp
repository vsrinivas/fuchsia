// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtests-utils/runtests-utils.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <launchpad/launchpad.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>


namespace runtests {

void record_test_result(list_node_t* tests, const char* name, test_result_t result, int rc) {
    size_t name_len = strlen(name) + 1;
    test_t* test = static_cast<test_t*>(malloc(sizeof(test_t) + name_len));
    test->result = result;
    test->rc = rc;
    memcpy(test->name, name, name_len);
    list_add_tail(tests, &test->node);
}


bool run_test(const char* path, signed char verbosity, FILE* out, list_node_t* tests) {
    int fds[2];
    // This arithmetic is invalid if verbosity < 0, but in that case by setting argc = 1.
    char verbose_opt[] = {'v','=', static_cast<char>(verbosity + '0'), 0};
    const char* argv[] = {path, verbose_opt};
    int argc = verbosity >= 0 ? 2 : 1;

    launchpad_t* lp = nullptr;
    zx_status_t status = ZX_OK;
    zx_handle_t test_job = ZX_HANDLE_INVALID;
    status = zx_job_create(zx_job_default(), 0, &test_job);
    if (status != ZX_OK) {
      printf("FAILURE: zx_job_create() returned %d\n", status);
      return false;
    }
    status = zx_object_set_property(test_job, ZX_PROP_NAME, "run-test", 8);
    if (status != ZX_OK) {
      printf("FAILURE: zx_object_set_property() returned %d\n", status);
      goto fail;
    }
    status = launchpad_create(test_job, path, &lp);
    if (status != ZX_OK) {
      printf("FAILURE: launchpad_create() returned %d\n", status);
      goto fail;
    }
    status = launchpad_load_from_file(lp, argv[0]);
    if (status != ZX_OK) {
      printf("FAILURE: launchpad_load_from_file() returned %d\n", status);
      goto fail;
    }
    status = launchpad_clone(lp, LP_CLONE_FDIO_ALL | LP_CLONE_ENVIRON);
    if (status != ZX_OK) {
      printf("FAILURE: launchpad_clone() returned %d\n", status);
      goto fail;
    }
    if (out != nullptr) {
        if (pipe(fds)) {
          printf("FAILURE: Failed to create pipe: %s\n", strerror(errno));
          goto fail;
        }
        status = launchpad_clone_fd(lp, fds[1], STDOUT_FILENO);
        if (status != ZX_OK) {
          printf("FAILURE: launchpad_clone_fd() returned %d\n", status);
          goto fail;
        }
        status = launchpad_transfer_fd(lp, fds[1], STDERR_FILENO);
        if (status != ZX_OK) {
          printf("FAILURE: launchpad_transfer_fd() returned %d\n", status);
          goto fail;
        }
    }
    launchpad_set_args(lp, argc, argv);
    const char* errmsg;
    zx_handle_t handle;
    status = launchpad_go(lp, &handle, &errmsg);
    lp = nullptr;
    if (status != ZX_OK) {
        printf("FAILURE: Failed to launch %s: %d: %s\n", path, status, errmsg);
        record_test_result(tests, path, FAILED_TO_LAUNCH, 0);
        goto fail;
    }
    // Tee output.
    if (out != nullptr) {
        char buf[1024];
        ssize_t bytes_read = 0;
        while ((bytes_read = read(fds[0], buf, 1024)) > 0) {
            fwrite(buf, 1, bytes_read, out);
            fwrite(buf, 1, bytes_read, stdout);
        }
    }
    status = zx_object_wait_one(handle, ZX_PROCESS_TERMINATED,
                                ZX_TIME_INFINITE, nullptr);
    if (status != ZX_OK) {
        printf("FAILURE: Failed to wait for process exiting %s: %d\n", path, status);
        record_test_result(tests, path, FAILED_TO_WAIT, 0);
        goto fail;
    }

    // read the return code
    zx_info_process_t proc_info;
    status = zx_object_get_info(handle, ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr);
    zx_handle_close(handle);

    if (status < 0) {
        printf("FAILURE: Failed to get process return code %s: %d\n", path, status);
        record_test_result(tests, path, FAILED_TO_RETURN_CODE, 0);
        goto fail;
    }

    if (proc_info.return_code != 0) {
        printf("FAILURE: %s exited with nonzero status: %d\n", path, proc_info.return_code);
        record_test_result(tests, path, FAILED_NONZERO_RETURN_CODE, proc_info.return_code);
        goto fail;
    }

    // TODO(garymm): May be missing a call to launchpad_destroy(lp), but just putting that right
    // before return results in page faults.
    zx_task_kill(test_job);
    zx_handle_close(test_job);
    printf("PASSED: %s passed\n", path);
    record_test_result(tests, path, SUCCESS, 0);
    return true;
fail:
    if (lp) {
      launchpad_destroy(lp);
    }
    zx_task_kill(test_job);
    zx_handle_close(test_job);
    return false;
}


}  // namespace runtests
