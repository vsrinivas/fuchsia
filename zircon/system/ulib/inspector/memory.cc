// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <pretty/hexdump.h>
#include <zircon/syscalls.h>

#include "inspector/inspector.h"
#include "utils-impl.h"

// How much memory we can dump at a time, in bytes.
// Space for this is allocated on the stack, so this can't be too large.
static constexpr size_t kMemoryDumpChunkSize = 256;
// The hexdump routines print 16 bytes at a time.
static constexpr size_t kHexdumpLineBytes = 16;
static_assert(kMemoryDumpChunkSize % kHexdumpLineBytes == 0, "");

 __EXPORT void inspector_print_memory(FILE* f, zx_handle_t process,
                                      zx_vaddr_t start, size_t length) {
    uint8_t buf[kMemoryDumpChunkSize];
    size_t bytes_read;

    zx_vaddr_t addr = start;
    for (size_t i = 0; i < length; i += kMemoryDumpChunkSize, addr += kMemoryDumpChunkSize) {
        size_t to_read = std::min(sizeof(buf), length - i);
        zx_status_t status = zx_process_read_memory(process, addr, buf,
                                                    to_read, &bytes_read);
        if (status < 0) {
            fprintf(f, "inspector: failed reading memory @0x%lx, error: %d\n",
                    addr, status);
            break;
        }
        if (bytes_read == 0) {
            fprintf(f, "inspector: zero bytes read @0x%lx\n", addr);
            break;
        }

        hexdump_very_ex(buf, bytes_read, addr, hexdump_stdio_printf, f);

        // If we got a short read we're done, no point in continuing.
        if (bytes_read < kMemoryDumpChunkSize)
            break;
    }
}
