// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "private.h"
#include "unistd.h"

#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <fdio/io.h>
#include <fdio/remoteio.h>
#include <fdio/vfs.h>

#define MIN_WINDOW (PAGE_SIZE * 4)
#define MAX_WINDOW ((size_t)64 << 20)

static zx_status_t read_at(fdio_t* io, void* buf, size_t len, off_t offset,
                           size_t* actual_len) {
    zx_status_t status;
    while ((status = fdio_read_at(io, buf, len, offset)) == ZX_ERR_SHOULD_WAIT) {
        status = fdio_wait(io, FDIO_EVT_READABLE, ZX_TIME_INFINITE, NULL);
        if (status != ZX_OK)
            return status;
    }
    if (status < 0)
        return status;
    if (status == 0) // EOF (?)
        return ZX_ERR_OUT_OF_RANGE;
    *actual_len = status;
    return ZX_OK;
}

static zx_status_t read_file_into_vmo(fdio_t* io, zx_handle_t* out_vmo) {
    zx_handle_t current_vmar_handle = zx_vmar_root_self();

    vnattr_t attr;
    int r = io->ops->misc(io, ZXRIO_STAT, 0, sizeof(attr), &attr, 0);
    if (r < 0)
        return ZX_ERR_BAD_HANDLE;
    if (r < (int)sizeof(attr))
        return ZX_ERR_IO;

    uint64_t size = attr.size;
    uint64_t offset = 0;

    zx_status_t status = zx_vmo_create(size, 0, out_vmo);
    if (status != ZX_OK)
        return status;

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
            size_t n;
            status = zx_vmo_write(*out_vmo, buffer, offset, nread, &n);
            if (status < 0) {
                zx_handle_close(*out_vmo);
                return status;
            }
            if (n != (size_t)nread) {
                zx_handle_close(*out_vmo);
                return ZX_ERR_IO;
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
    zx_handle_t vmo;
    size_t offset, len;
    zx_status_t status = io->ops->get_vmo(io, &vmo, &offset, &len);
    if (status != ZX_OK)
        return status;
    // Clone a private copy of it at the offset/length returned with
    // the handle.
    // TODO(mcgrathr): Create a plain read only clone when the feature
    // is implemented in the VM.
    status = zx_vmo_clone(vmo, ZX_VMO_CLONE_COPY_ON_WRITE, offset, len, out_vmo);
    zx_handle_close(vmo);
    return status;
}

zx_status_t fdio_get_vmo(int fd, zx_handle_t* out_vmo) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL)
        return ZX_ERR_BAD_HANDLE;

    zx_handle_t vmo;
    zx_status_t status = get_file_vmo(io, &vmo);
    if (status != ZX_OK)
        status = read_file_into_vmo(io, &vmo);
    fdio_release(io);

    if (status == ZX_OK) {
        status = zx_handle_replace(
            vmo,
            ZX_RIGHT_READ | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP |
            ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE |
            ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_SET_PROPERTY,
            out_vmo);
        if (status != ZX_OK)
            zx_handle_close(vmo);
    }

    return status;
}

zx_status_t fdio_get_exact_vmo(int fd, zx_handle_t* out_vmo) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL)
        return ZX_ERR_BAD_HANDLE;

    zx_handle_t vmo;
    size_t offset, len;
    zx_status_t status = io->ops->get_vmo(io, &vmo, &offset, &len);
    fdio_release(io);

    if (status != ZX_OK)
        return status;

    size_t vmo_size;
    if (offset != 0 || zx_vmo_get_size(vmo, &vmo_size) != ZX_OK || vmo_size != len) {
        zx_handle_close(vmo);
        return ZX_ERR_NOT_FOUND;
     }

    *out_vmo = vmo;
    return ZX_OK;
}
