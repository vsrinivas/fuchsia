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

void do_indent(unsigned n) {
    while(n > 0) {
        printf("  ");
        n--;
    }
}

void list_jobs(mx_handle_t job, unsigned indent) {
    mx_koid_t koids[128];
    size_t actual;
    size_t avail;

    if (mx_object_get_info(job, MX_INFO_JOB_CHILDREN, koids, sizeof(koids), &actual, &avail) < 0) {
        return;
    }

    for (size_t n = 0; n < actual; n++) {
        do_indent(indent);
        printf("job  %05" PRIu64 " \n", koids[n]);
        mx_handle_t child;
        if (mx_object_get_child(job, koids[n], MX_RIGHT_SAME_RIGHTS, &child) == NO_ERROR) {
            list_jobs(child, indent + 1);
            mx_handle_close(child);
        }
    }

    if (mx_object_get_info(job, MX_INFO_JOB_PROCESSES, koids, sizeof(koids), &actual, &avail) < 0) {
        return;
    }

    for (size_t n = 0; n < actual; n++) {
        char name[MX_MAX_NAME_LEN];
        mx_handle_t child;
        name[0] = 0;
        if (mx_object_get_child(job, koids[n], MX_RIGHT_SAME_RIGHTS, &child) == NO_ERROR) {
            mx_object_get_property(child, MX_PROP_NAME, name, sizeof(name));
            mx_handle_close(child);
        }
        do_indent(indent);
        printf("proc %05" PRIu64 " '%s'\n", koids[n], name);
    }

}

int main(int argc, char** argv) {
    int fd = open("/dev/class/misc/sysinfo", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "ps: cannot open sysinfo: %d\n", errno);
        return -1;
    }
    mx_handle_t root_job;
    if (ioctl_sysinfo_get_root_job(fd, &root_job) != sizeof(root_job)) {
        fprintf(stderr, "ps: cannot obtain root job\n");
        return -1;
    }
    close(fd);

    printf("job  root\n");
    list_jobs(root_job, 1);
    mx_handle_close(root_job);
}