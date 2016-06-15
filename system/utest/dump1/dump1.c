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

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <mxio/io.h>

int main(int argc, char **argv) {
    unsigned char x;
    int fd = 0;
    if (argc == 2) {
        fd = open(argv[1], O_RDONLY);
        if (fd < 0) {
            printf("dump1: cannot open '%s'\n", argv[1]);
            return -1;
        }
    }
    for (;;) {
        mxio_wait_fd(fd, MXIO_EVT_READABLE, NULL);
        int r = read(fd, &x, 1);
        if (r == 0) {
            continue;
        }
        if (r != 1) {
            break;
        }
        if (x == 'z') {
            break;
        }
        printf("%02x ", x);
        fflush(stdout);
    }
    printf("\n");
    return 0;
}
