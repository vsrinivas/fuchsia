// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootdata.h"
#include "util.h"

#pragma GCC visibility push(hidden)

#include <bootdata/decompress.h>
#include <magenta/boot/bootdata.h>
#include <magenta/syscalls.h>
#include <string.h>

#pragma GCC visibility pop

mx_handle_t bootdata_get_bootfs(mx_handle_t log, mx_handle_t vmar_self,
                                mx_handle_t bootdata_vmo) {
    size_t off = 0;
    for (;;) {
        bootdata_t bootdata;
        size_t actual;
        mx_status_t status = mx_vmo_read(bootdata_vmo, &bootdata,
                                         off, sizeof(bootdata), &actual);
        check(log, status, "mx_vmo_read failed on bootdata VMO");
        if (actual != sizeof(bootdata))
            fail(log, "short read on bootdata VMO");

        size_t hdrsz = sizeof(bootdata);
        if (bootdata.flags & BOOTDATA_FLAG_EXTRA) {
            hdrsz += sizeof(bootextra_t);
        }

        switch (bootdata.type) {
        case BOOTDATA_CONTAINER:
            if (off == 0) {
                // Quietly skip container header.
                bootdata.length = 0;
            } else {
                fail(log, "container in the middle of bootdata");
            }
            break;

        case BOOTDATA_BOOTFS_BOOT:;
            const char* errmsg;
            mx_handle_t bootfs_vmo;
            status = decompress_bootdata(vmar_self, bootdata_vmo, off,
                                         bootdata.length + hdrsz,
                                         &bootfs_vmo, &errmsg);
            check(log, status, "%s", errmsg);

            // Signal that we've already processed this one.
            bootdata.type = BOOTDATA_BOOTFS_DISCARD;
            check(log, mx_vmo_write(bootdata_vmo, &bootdata.type,
                                    off + offsetof(bootdata_t, type),
                                    sizeof(bootdata.type), &actual),
                  "mx_vmo_write failed on bootdata VMO\n");

            return bootfs_vmo;
        }

        off += BOOTDATA_ALIGN(hdrsz + bootdata.length);
    }

    fail(log, "no '/boot' bootfs in bootstrap message\n");
}
