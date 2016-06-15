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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <magenta/syscalls.h>

#include <mxio/io.h>
#include <mxio/util.h>

int main(int argc, char** argv) {
    mx_handle_t p1a, p1b, p2a, p2b;
    uintptr_t entry;
    mx_handle_t p;
    mx_status_t r;
    int fd;

    if ((p1a = _magenta_message_pipe_create(&p1b)) < 0) {
        printf("failed to create pipe1 %d\n", p1a);
        return -1;
    }
    if ((p2a = _magenta_message_pipe_create(&p2b)) < 0) {
        printf("failed to create pipe2 %d\n", p2a);
        return -1;
    }

    // send a message and p2b through p1a
    if ((r = _magenta_message_write(p1a, "hello", 6, &p2b, 1, 0)) < 0) {
        printf("failed to write message+handle to p1a %d\n", r);
    }

    // create helper process and pass p1b across to it
    if ((p = _magenta_process_create("helper", 7)) < 0) {
        printf("couldn't create process %d\n", p);
        return -1;
    }
    if ((fd = open("/boot/bin/reply-handle-helper", O_RDONLY)) < 0) {
        printf("couldn't open reply-handle-helper %d\n", fd);
        return -1;
    }
    if ((r = mxio_load_elf_fd(p, &entry, fd)) < 0) {
        printf("couldn't load reply-handle-helper %d\n", r);
        return -1;
    }
    if ((r = _magenta_process_start(p, p1b, entry)) < 0) {
        printf("process did not start %d\n", r);
        return -1;
    }

    mx_signals_t pending;
    if ((r = _magenta_handle_wait_one(p2a, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                      MX_TIME_INFINITE, &pending, NULL)) < 0) {
        printf("error waiting on p2a %d\n", r);
        return -1;
    }
    if (!(pending & MX_SIGNAL_READABLE)) {
        printf("pipe 2a not readable\n");
        return -1;
    }

    printf("write handle %x to helper...\n", p2b);
    char data[128];
    mx_handle_t h;
    uint32_t dsz = sizeof(data) - 1;
    uint32_t hsz = 1;
    if ((r = _magenta_message_read(p2a, data, &dsz, &h, &hsz, 0)) < 0) {
        printf("failed to read reply %d\n", r);
        return -1;
    }
    data[dsz] = 0;
    printf("reply: '%s' %u %u\n", data, dsz, hsz);
    if (hsz != 1) {
        printf("no handle returned\n");
        return -1;
    }
    printf("read handle %x from reply port\n", h);
    if (h != p2b) {
        printf("different handle returned\n");
        return -1;
    }

    printf("success\n");
    return 0;
}
