// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>

int main(int argc, char** argv) {
    bool tail = false;
    mx_handle_t h;

    if ((argc == 2) && (!strcmp(argv[1], "-f"))) {
        tail = true;
    }
    if (mx_log_create(MX_LOG_FLAG_READABLE, &h) < 0) {
        printf("dlog: cannot open log\n");
    }

    char buf[MX_LOG_RECORD_MAX];
    mx_log_record_t* rec = (mx_log_record_t*)buf;
    for (;;) {
        mx_status_t status;
        if ((status = mx_log_read(h, MX_LOG_RECORD_MAX, rec, 0)) < 0) {
            if ((status == MX_ERR_SHOULD_WAIT) && tail) {
                mx_object_wait_one(h, MX_LOG_READABLE, MX_TIME_INFINITE, NULL);
                continue;
            }
            break;
        }
        char tmp[32];
        size_t len = snprintf(tmp, sizeof(tmp), "[%05d.%03d] %c ",
                            (int)(rec->timestamp / 1000000000ULL),
                            (int)((rec->timestamp / 1000000ULL) % 1000ULL),
                            (rec->flags & MX_LOG_FLAG_KERNEL) ? 'K' : 'U');
        write(1, tmp, (len > sizeof(tmp) ? sizeof(tmp) : len));
        write(1, rec->data, rec->datalen);
        if ((rec->datalen == 0) || (rec->data[rec->datalen - 1] != '\n')) {
            write(1, "\n", 1);
        }
    }
    return 0;
}
