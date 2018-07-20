// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "private.h"
#include "unistd.h"

#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <lib/fdio/io.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/vfs.h>

#define MIN_WINDOW (PAGE_SIZE * 4)
#define MAX_WINDOW ((size_t)64 << 20)

static zx_status_t read_at(fdio_t* io, void* buf, size_t len, off_t offset,
                           size_t* actual_len) {
    zx_status_t status;
    while ((status = fdio_read_at(io, buf, len, offset)) == ZX_ERR_SHOULD_WAIT) {
        status = fdio_wait(io, FDIO_EVT_READABLE, ZX_TIME_INFINITE, NULL);
        if (status != ZX_OK) {
            return status;
        }
    }
    if (status < 0) {
        return status;
    }
    if (status == 0) { // EOF (?)
        return ZX_ERR_OUT_OF_RANGE;
    }
    *actual_len = status;
    return ZX_OK;
}

static zx_status_t read_file_into_vmo(fdio_t* io, zx_handle_t* out_vmo) {
    zx_handle_t current_vmar_handle = zx_vmar_root_self();

    vnattr_t attr;
    int r = io->ops->misc(io, ZXFIDL_STAT, 0, sizeof(attr), &attr, 0);
    if (r < 0) {
        return ZX_ERR_BAD_HANDLE;
    }
    if (r < (int)sizeof(attr)) {
        return ZX_ERR_IO;
    }

    uint64_t size = attr.size;
    uint64_t offset = 0;

    zx_status_t status = zx_vmo_create(size, 0, out_vmo);
    if (status != ZX_OK) {
        return status;
    }

    while (size > 0) {
        if (size < MIN_WINDOW) {
            // There is little enough left that copying is less overhead
            // than fiddling with the page tables.
            char buffer[PAGE_SIZE];
            size_t xfer = size < sizeof(buffer) ? size : sizeof(buffer);
            size_t nread;
            status = read_at(io, buffer, xfer, offset, &nread);
            if (status != ZX_OK) {
                zx_handle_close(*out_vmo);
                return status;
            }
            status = zx_vmo_write(*out_vmo, buffer, offset, nread);
            if (status < 0) {
                zx_handle_close(*out_vmo);
                return status;
            }
            offset += nread;
            size -= nread;
        } else {
            // Map the VMO into our own address space so we can read into
            // it directly and avoid double-buffering.
            size_t chunk = size < MAX_WINDOW ? size : MAX_WINDOW;
            size_t window = (chunk + PAGE_SIZE - 1) & -PAGE_SIZE;
            uintptr_t start = 0;
            status = zx_vmar_map(
                current_vmar_handle, 0, *out_vmo, offset, window,
                ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &start);
            if (status != ZX_OK) {
                zx_handle_close(*out_vmo);
                return status;
            }
            uint8_t* buffer = (void*)start;
            while (chunk > 0) {
                size_t nread;
                status = read_at(io, buffer, chunk, offset, &nread);
                if (status != ZX_OK) {
                    zx_vmar_unmap(current_vmar_handle, start, window);
                    zx_handle_close(*out_vmo);
                    return status;
                }
                buffer += nread;
                offset += nread;
                size -= nread;
                chunk -= nread;
            }
            zx_vmar_unmap(current_vmar_handle, start, window);
        }
    }

    return ZX_OK;
}

static zx_status_t get_file_vmo(fdio_t* io, zx_handle_t* out_vmo) {
    return io->ops->get_vmo(io, FDIO_MMAP_FLAG_READ | FDIO_MMAP_FLAG_EXEC |
                            FDIO_MMAP_FLAG_PRIVATE, out_vmo);
}

static zx_status_t copy_file_vmo(fdio_t* io, zx_handle_t* out_vmo) {
    zx_status_t status = get_file_vmo(io, out_vmo);
    if (status == ZX_OK) {
        return ZX_OK;
    }

    zx_handle_t vmo;
    if ((status = read_file_into_vmo(io, &vmo)) == ZX_OK) {
        status = zx_handle_replace(
            vmo,
            ZX_RIGHTS_BASIC | ZX_RIGHTS_PROPERTY |
            ZX_RIGHT_READ | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP,
            out_vmo);
    }
    return status;
}

zx_status_t fdio_get_vmo_copy(int fd, zx_handle_t* out_vmo) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ZX_ERR_BAD_HANDLE;
    }
    zx_status_t status = copy_file_vmo(io, out_vmo);
    fdio_release(io);
    return status;
}

zx_status_t fdio_get_vmo_clone(int fd, zx_handle_t* out_vmo) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ZX_ERR_BAD_HANDLE;
    }
    zx_status_t status = get_file_vmo(io, out_vmo);
    fdio_release(io);
    return status;
}

zx_status_t fdio_get_vmo(int fd, zx_handle_t* out_vmo) {
    return fdio_get_vmo_copy(fd, out_vmo);
}

zx_status_t fdio_get_vmo_exact(int fd, zx_handle_t* out_vmo) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ZX_ERR_BAD_HANDLE;
    }

    zx_status_t status = io->ops->get_vmo(io, FDIO_MMAP_FLAG_READ | FDIO_MMAP_FLAG_EXEC |
                                          FDIO_MMAP_FLAG_EXACT, out_vmo);
    fdio_release(io);

    return status;
}

zx_status_t fdio_get_exact_vmo(int fd, zx_handle_t* out_vmo) {
    return fdio_get_vmo_exact(fd, out_vmo);
}
