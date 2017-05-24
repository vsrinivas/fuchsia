// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "private.h"
#include "unistd.h"

#include <magenta/process.h>
#include <magenta/syscalls.h>

#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>

#define MIN_WINDOW (PAGE_SIZE * 4)
#define MAX_WINDOW ((size_t)64 << 20)

static mx_status_t read_at(mxio_t* io, void* buf, size_t len, off_t offset,
                           size_t* actual_len) {
    mx_status_t status;
    while ((status = mxio_read_at(io, buf, len, offset)) == ERR_SHOULD_WAIT) {
        status = mxio_wait(io, MXIO_EVT_READABLE, MX_TIME_INFINITE, NULL);
        if (status != NO_ERROR)
            return status;
    }
    if (status < 0)
        return status;
    if (status == 0) // EOF (?)
        return ERR_OUT_OF_RANGE;
    *actual_len = status;
    return NO_ERROR;
}

static mx_status_t read_file_into_vmo(mxio_t* io, mx_handle_t* out_vmo) {
    mx_handle_t current_vmar_handle = mx_vmar_root_self();

    vnattr_t attr;
    int r = io->ops->misc(io, MXRIO_STAT, 0, sizeof(attr), &attr, 0);
    if (r < 0)
        return ERR_BAD_HANDLE;
    if (r < (int)sizeof(attr))
        return ERR_IO;

    uint64_t size = attr.size;
    uint64_t offset = 0;

    mx_status_t status = mx_vmo_create(size, 0, out_vmo);
    if (status != NO_ERROR)
        return status;

    while (size > 0) {
        if (size < MIN_WINDOW) {
            // There is little enough left that copying is less overhead
            // than fiddling with the page tables.
            char buffer[PAGE_SIZE];
            size_t xfer = size < sizeof(buffer) ? size : sizeof(buffer);
            size_t nread;
            status = read_at(io, buffer, xfer, offset, &nread);
            if (status != NO_ERROR) {
                mx_handle_close(*out_vmo);
                return status;
            }
            size_t n;
            status = mx_vmo_write(*out_vmo, buffer, offset, nread, &n);
            if (status < 0) {
                mx_handle_close(*out_vmo);
                return status;
            }
            if (n != (size_t)nread) {
                mx_handle_close(*out_vmo);
                return ERR_IO;
            }
            offset += nread;
            size -= nread;
        } else {
            // Map the VMO into our own address space so we can read into
            // it directly and avoid double-buffering.
            size_t chunk = size < MAX_WINDOW ? size : MAX_WINDOW;
            size_t window = (chunk + PAGE_SIZE - 1) & -PAGE_SIZE;
            uintptr_t start = 0;
            status = mx_vmar_map(
                current_vmar_handle, 0, *out_vmo, offset, window,
                MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &start);
            if (status != NO_ERROR) {
                mx_handle_close(*out_vmo);
                return status;
            }
            uint8_t* buffer = (void*)start;
            while (chunk > 0) {
                size_t nread;
                status = read_at(io, buffer, chunk, offset, &nread);
                if (status != NO_ERROR) {
                    mx_vmar_unmap(current_vmar_handle, start, window);
                    mx_handle_close(*out_vmo);
                    return status;
                }
                buffer += nread;
                offset += nread;
                size -= nread;
                chunk -= nread;
            }
            mx_vmar_unmap(current_vmar_handle, start, window);
        }
    }

    return NO_ERROR;
}

static mx_status_t get_file_vmo(mxio_t* io, mx_handle_t* out_vmo) {
    mx_handle_t vmo;
    size_t offset, len;
    mx_status_t status = io->ops->get_vmo(io, &vmo, &offset, &len);
    if (status == NO_ERROR) {
        // If the file spans the whole VMO, just return the original
        // VMO handle, which is already read-only.  This is more
        // than an optimization in the case where the specific VMO
        // is magical like the vDSO VMOs.
        size_t vmo_size;
        if (offset == 0 &&
            mx_vmo_get_size(vmo, &vmo_size) == NO_ERROR &&
            vmo_size == len) {
            *out_vmo = vmo;
        } else {
            // Clone a private copy of it at the offset/length returned with
            // the handle.
            // TODO(mcgrathr): Create a plain read only clone when the feature
            // is implemented in the VM.
            status = mx_vmo_clone(vmo, MX_VMO_CLONE_COPY_ON_WRITE, offset, len,
                                  out_vmo);
            mx_handle_close(vmo);
        }
    }
    return status;
}

mx_status_t mxio_get_vmo(int fd, mx_handle_t* out_vmo) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL)
        return ERR_BAD_HANDLE;

    mx_handle_t vmo;
    mx_status_t status = get_file_vmo(io, &vmo);
    if (status != NO_ERROR)
        status = read_file_into_vmo(io, &vmo);
    mxio_release(io);

    if (status == NO_ERROR) {
        // Drop unnecessary WRITE rights on the VMO handle.
        status = mx_handle_replace(
            vmo,
            MX_RIGHT_READ | MX_RIGHT_EXECUTE | MX_RIGHT_MAP |
            MX_RIGHT_TRANSFER | MX_RIGHT_DUPLICATE | MX_RIGHT_GET_PROPERTY,
            out_vmo);
        if (status != NO_ERROR)
            mx_handle_close(vmo);
    }

    return status;
}
