// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <magenta/device/ktrace.h>

// 1. Run:            magenta> traceme
// 2. Stop tracing:   magenta> dm ktraceoff
// 3. Grab trace:     host> netcp :/dev/class/misc/ktrace test.trace
// 4. Examine trace:  host> tracevic test.trace

int main(int argc, char** argv) {
    int fd;
    if ((fd = open("/dev/class/misc/ktrace", O_RDWR)) < 0) {
        fprintf(stderr, "cannot open trace device\n");
        return -1;
    }

    // obtain the handle needed to emit probes
    mx_handle_t kth;
    if (ioctl_ktrace_get_handle(fd, &kth) < 0) {
        fprintf(stderr, "cannot get ktrace handle\n");
        return -1;
    }

    // for each probe/event, register its name and get its id
    uint32_t id;
    if (ioctl_ktrace_add_probe(fd, "trace-me", &id) < 0) {
        fprintf(stderr, "cannot register ktrace probe\n");
        return -1;
    }

    // once all probes are registered, you can close the device
    close(fd);

    // use the ktrace handle to emit probes into the trace stream
    mx_ktrace_write(kth, id, 1, 0);
    printf("hello, ktrace! id = %u\n", id);
    mx_ktrace_write(kth, id, 2, 0);

    return 0;
}

