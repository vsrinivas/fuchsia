// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <magenta/types.h>
#include <mxio/util.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

int main(int argc, const char** argv) {
    mx_handle_t handles[2 * MXIO_MAX_HANDLES];
    uint32_t ids[2 * MXIO_MAX_HANDLES];
    mx_status_t r;
    size_t n = 0;

    int fd = open("/dev/class/console/vc", O_RDWR);
    if (fd < 0) {
        printf("Error %d opening a new vc\n", fd);
        return fd;
    }

    r = mxio_clone_root(handles, ids);
    if (r < 0) {
        printf("Error %d in mxio_clone_root\n", r);
        return r;
    }
    n += r;

    r = mxio_clone_fd(fd, fd, handles + n, ids + n);
    if (r < 0) {
        printf("Error %d in mxio_clone_fd\n", r);
        return r;
    }
    for (int i = 0; i < r; i++) {
        ids[n + i] |= (MXIO_FLAG_USE_FOR_STDIO << 16);
    }
    n += r;

    // start mxsh if no arguments
    char pname[128];
    int pargc;
    bool mxsh;
    const char* pargv[1] = { "/boot/bin/mxsh" };
    if ((mxsh = argc == 1)) {
        strcpy(pname, "mxsh:vc");
        pargc = 1;
    } else {
        char* bname = strrchr(argv[1], '/');
        snprintf(pname, sizeof(pname), "%s:vc", bname ? bname + 1 : argv[1]);
        pargc = argc - 1;
    }
    printf("starting process %s\n", pargv[0]);
    mxio_start_process_etc(pname, pargc, mxsh ? pargv : &argv[1], n, handles, ids);

    close(fd);
    return 0;
}
