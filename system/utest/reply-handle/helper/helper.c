// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/util.h>
#include <stdio.h>

int main(void) {
    char data[128];
    mx_handle_t h0, h1;
    uint32_t dsz, hsz;
    mx_status_t r;

    puts("helper: start");

    h0 = mxio_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_USER0, 0));
    if (h0 < 0) {
        printf("helper: mxio_get_startup_handle failed: %d\n", h0);
        return -1;
    }

    dsz = sizeof(data);
    hsz = 1;
    if ((r = mx_message_read(h0, data, &dsz, &h1, &hsz, 0)) < 0) {
        printf("helper: failed to read message %d\n", r);
        return -1;
    }
    if (hsz != 1) {
        printf("no handle received\n");
        return -1;
    }
    if ((r = mx_message_write(h1, "okay", 5, &h1, 1, 0)) < 0) {
        printf("helper: failed to write message %d\n", r);
        mx_message_write(h1, "fail", 5, NULL, 0, 0);
        return -1;
    }
    puts("helper: done");
    return 0;
}
