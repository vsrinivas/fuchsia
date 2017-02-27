// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <stdio.h>

#include "processes.h"

void do_indent(unsigned n) {
    while(n > 0) {
        printf("  ");
        n--;
    }
}

mx_status_t job_callback(int depth, mx_handle_t job, mx_koid_t koid) {
    char name[MX_MAX_NAME_LEN];
    mx_status_t status = mx_object_get_property(job, MX_PROP_NAME, name, sizeof(name));
    if (status != NO_ERROR) {
      return status;
    }
    do_indent(depth + 1);
    printf("job  %-10" PRIu64 " '%s'\n", koid, name);
    return NO_ERROR;
}

mx_status_t process_callback(int depth, mx_handle_t process, mx_koid_t koid) {
    char name[MX_MAX_NAME_LEN];
    mx_status_t status = mx_object_get_property(process, MX_PROP_NAME, name, sizeof(name));
    if (status != NO_ERROR) {
      return status;
    }
    do_indent(depth + 1);
    printf("proc %-10" PRIu64 " '%s'\n", koid, name);
    return NO_ERROR;
}

int main(int argc, char** argv) {
    printf("job  root\n");

    walk_process_tree(job_callback, process_callback);
}
