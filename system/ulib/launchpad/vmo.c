// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/vmo.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <magenta/syscalls.h>
#include <sys/stat.h>
#include <unistd.h>

mx_handle_t launchpad_vmo_from_mem(const void* data, size_t len) {
    mx_handle_t vmo;
    mx_status_t status = mx_vmo_create(len, 0, &vmo);
    if (status < 0)
        return status;
    size_t n;
    status = mx_vmo_write(vmo, data, 0, len, &n);
    if (status < 0) {
        mx_handle_close(vmo);
        return status;
    }
    if (n != len) {
        mx_handle_close(vmo);
        return ERR_IO;
    }
    return vmo;
}

static ssize_t my_pread (int fd, void* buf, size_t nbytes, off_t offset) {
    if (lseek(fd, offset, SEEK_SET) != offset)
        return -1;
    return read(fd, buf, nbytes);
}

#define MIN_WINDOW (PAGE_SIZE * 4)
#define MAX_WINDOW ((size_t)64 << 20)

mx_handle_t launchpad_vmo_from_fd(int fd) {
    mx_handle_t current_vmar_handle = mx_vmar_root_self();

    struct stat st;
    if (fstat(fd, &st) < 0)
        return ERR_IO;

    uint64_t size = st.st_size;
    uint64_t offset = 0;

    mx_handle_t vmo;
    mx_status_t status = mx_vmo_create(size, 0, &vmo);
    if (status < 0)
        return status;

    while (size > 0) {
        if (size < MIN_WINDOW) {
            // There is little enough left that copying is less overhead
            // than fiddling with the page tables.
            char buffer[PAGE_SIZE];
            size_t xfer = size < sizeof(buffer) ? size : sizeof(buffer);
            ssize_t nread = my_pread(fd, buffer, xfer, offset);
            if (nread < 0) {
                mx_handle_close(vmo);
                return ERR_IO;
            }
            if (nread == 0) {
                mx_handle_close(vmo);
                errno = ESPIPE;
                return ERR_IO;
            }
            size_t n;
            status = mx_vmo_write(vmo, buffer, offset, nread, &n);
            if (status < 0) {
                mx_handle_close(vmo);
                return status;
            }
            if (n != (size_t)nread) {
                mx_handle_close(vmo);
                errno = 0;
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
            mx_status_t status = mx_vmar_map(
                current_vmar_handle, 0, vmo, offset, window,
                MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &start);
            if (status < 0) {
                mx_handle_close(vmo);
                return status;
            }
            uint8_t* buffer = (void*)start;
            while (chunk > 0) {
                ssize_t nread = my_pread(fd, buffer, chunk, offset);
                if (nread < 0) {
                    mx_vmar_unmap(current_vmar_handle, start, window);
                    mx_handle_close(vmo);
                    return ERR_IO;
                }
                if (nread == 0) {
                    mx_vmar_unmap(current_vmar_handle, start, window);
                    mx_handle_close(vmo);
                    errno = ESPIPE;
                    return ERR_IO;
                }
                buffer += nread;
                offset += nread;
                size -= nread;
                chunk -= nread;
            }
            mx_vmar_unmap(current_vmar_handle, start, window);
        }
    }

    return vmo;
}

mx_handle_t launchpad_vmo_from_file(const char* filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
        return ERR_IO;
    mx_handle_t vmo = launchpad_vmo_from_fd(fd);
    close(fd);
    return vmo;
}
