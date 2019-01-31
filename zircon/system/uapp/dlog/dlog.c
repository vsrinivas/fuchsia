// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

void usage(void) {
    fprintf(stderr,
        "usage: dlog        dump the zircon debug log\n"
        "\n"
        "options: -f        don't exit, keep waiting for new messages\n"
        "         -p <pid>  only show messages from specified pid\n"
        "         -t        only show the text of messages (no metadata)\n"
        "         -h        show help\n"
        );
}

int main(int argc, char** argv) {
    bool tail = false;
    bool filter_pid = false;
    bool plain = false;
    zx_koid_t pid = 0;
    zx_handle_t h;

    while (argc > 1) {
        if (!strcmp(argv[1], "-h")) {
            usage();
            return 0;
        } else if (!strcmp(argv[1], "-f")) {
            tail = true;
        } else if (!strcmp(argv[1], "-t")) {
            plain = true;
        } else if (!strcmp(argv[1], "-p")) {
            argc--;
            argv++;
            if (argc < 2) {
                usage();
                return -1;
            }
            errno = 0;
            pid = strtoull(argv[1], NULL, 0);
            if (errno) {
                fprintf(stderr, "dlog: invalid pid\n");
                return -1;
            }
            filter_pid = true;
        } else {
            usage();
            return -1;
        }
        argc--;
        argv++;
    }

    if (zx_debuglog_create(ZX_HANDLE_INVALID, ZX_LOG_FLAG_READABLE, &h) < 0) {
        fprintf(stderr, "dlog: cannot open debug log\n");
        return -1;
    }

    char buf[ZX_LOG_RECORD_MAX];
    zx_log_record_t* rec = (zx_log_record_t*)buf;
    for (;;) {
        zx_status_t status;
        if ((status = zx_debuglog_read(h, 0, rec, ZX_LOG_RECORD_MAX)) < 0) {
            if ((status == ZX_ERR_SHOULD_WAIT) && tail) {
                zx_object_wait_one(h, ZX_LOG_READABLE, ZX_TIME_INFINITE, NULL);
                continue;
            }
            break;
        }
        if (filter_pid && (pid != rec->pid)) {
            continue;
        }
        if (!plain) {
            char tmp[32];
            size_t len = snprintf(tmp, sizeof(tmp), "[%05d.%03d] ",
                                  (int)(rec->timestamp / 1000000000ULL),
                                  (int)((rec->timestamp / 1000000ULL) % 1000ULL));
            write(1, tmp, (len > sizeof(tmp) ? sizeof(tmp) : len));
        }
        write(1, rec->data, rec->datalen);
        if ((rec->datalen == 0) || (rec->data[rec->datalen - 1] != '\n')) {
            write(1, "\n", 1);
        }
    }
    return 0;
}
