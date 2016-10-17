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
    if ((h = mx_log_create(MX_LOG_FLAG_READABLE)) < 0) {
        printf("dlog: cannot open log\n");
    }

    char buf[MX_LOG_RECORD_MAX];
    mx_log_record_t* rec = (mx_log_record_t*)buf;
    for (;;) {
        if (mx_log_read(h, MX_LOG_RECORD_MAX, rec, tail ? MX_LOG_FLAG_WAIT : 0) > 0) {
            char tmp[64];
            snprintf(tmp, 64, "[%05d.%03d] %c ",
                     (int)(rec->timestamp / 1000000000ULL),
                     (int)((rec->timestamp / 1000000ULL) % 1000ULL),
                     (rec->flags & MX_LOG_FLAG_KERNEL) ? 'K' : 'U');
            write(1, tmp, strlen(tmp));
            write(1, rec->data, rec->datalen);
            if ((rec->datalen == 0) || (rec->data[rec->datalen - 1] != '\n')) {
                write(1, "\n", 1);
            }
        } else {
            break;
        }
    }
    return 0;
}
