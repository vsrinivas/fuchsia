// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>

#include <magenta/device/sysinfo.h>

// start from the passed in job handle, drilling down looking for a paricular task id
static mx_status_t find_task(mx_handle_t job, mx_koid_t task_id, mx_handle_t *handle) {
    mx_koid_t koids[128];
    size_t actual;
    size_t avail;

    // get a list of child jobs for this job
    if (mx_object_get_info(job, MX_INFO_JOB_CHILDREN, koids, sizeof(koids), &actual, &avail) < 0) {
        return ERR_NOT_FOUND;
    }

    // drill down into the job tree
    for (size_t n = 0; n < actual; n++) {
        mx_handle_t child;
        if (mx_object_get_child(job, koids[n], MX_RIGHT_SAME_RIGHTS, &child) == NO_ERROR) {
            // see if this koid matches
            if (koids[n] == task_id) {
                *handle = child;
                return NO_ERROR;
            }

            // recurse to its children
            if (find_task(child, task_id, handle) == NO_ERROR)
                return NO_ERROR;
            mx_handle_close(child);
        }
    }

    // get the list of processes under this job
    if (mx_object_get_info(job, MX_INFO_JOB_PROCESSES, koids, sizeof(koids), &actual, &avail) < 0) {
        return ERR_NOT_FOUND;
    }

    for (size_t n = 0; n < actual; n++) {
        mx_handle_t child;
        if (mx_object_get_child(job, koids[n], MX_RIGHT_SAME_RIGHTS, &child) == NO_ERROR) {
            // see if this koid matches
            if (koids[n] == task_id) {
                *handle = child;
                return NO_ERROR;
            }

            mx_handle_close(child);
        }
    }

    return ERR_NOT_FOUND;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <task id>\n", argv[0]);
        return -1;
    }

    mx_koid_t task_id = atoll(argv[1]);

    int fd = open("/dev/class/misc/sysinfo", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "%s: cannot open sysinfo: %d\n", argv[0], errno);
        return -1;
    }
    mx_handle_t root_job;
    if (ioctl_sysinfo_get_root_job(fd, &root_job) != sizeof(root_job)) {
        fprintf(stderr, "%s: cannot obtain root job\n", argv[0]);
        return -1;
    }
    close(fd);

    mx_handle_t handle;
    mx_status_t status = find_task(root_job, task_id, &handle);
    mx_handle_close(root_job);

    if (status != NO_ERROR) {
        fprintf(stderr, "no task found\n");
        return -1;
    }

    // mark the task for kill
    mx_task_kill(handle);
    mx_handle_close(handle);
}
