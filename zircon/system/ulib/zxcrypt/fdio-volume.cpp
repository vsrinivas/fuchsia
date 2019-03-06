// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>

#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
#include <fuchsia/device/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <lib/zircon-internal/debug.h>
#include <ramdevice-client/ramdisk.h> // Why does wait_for_device() come from here?
#include <zircon/status.h>
#include <zxcrypt/fdio-volume.h>
#include <zxcrypt/volume.h>

#include <utility>

#define ZXDEBUG 0

namespace zxcrypt {

// The zxcrypt driver
const char* kDriverLib = "/boot/driver/zxcrypt.so";

FdioVolume::FdioVolume(fbl::unique_fd&& fd) : Volume(), fd_(std::move(fd)) {}

zx_status_t FdioVolume::Init(fbl::unique_fd fd, fbl::unique_ptr<FdioVolume>* out) {
    zx_status_t rc;

    if (!fd || !out) {
        xprintf("bad parameter(s): fd=%d, out=%p\n", fd.get(), out);
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<FdioVolume> volume(new (&ac) FdioVolume(std::move(fd)));
    if (!ac.check()) {
        xprintf("allocation failed: %zu bytes\n", sizeof(FdioVolume));
        return ZX_ERR_NO_MEMORY;
    }

    if ((rc = volume->Init()) != ZX_OK) {
        return rc;
    }

    *out = std::move(volume);
    return ZX_OK;
}

zx_status_t FdioVolume::Create(fbl::unique_fd fd, const crypto::Secret& key,
                                   fbl::unique_ptr<FdioVolume>* out) {
    zx_status_t rc;

    fbl::unique_ptr<FdioVolume> volume;
    if ((rc = FdioVolume::Init(std::move(fd), &volume)) != ZX_OK ||
        (rc = volume->CreateBlock()) != ZX_OK || (rc = volume->SealBlock(key, 0)) != ZX_OK ||
        (rc = volume->CommitBlock()) != ZX_OK) {
        return rc;
    }

    if (out) {
        *out = std::move(volume);
    }
    return ZX_OK;
}

zx_status_t FdioVolume::Unlock(fbl::unique_fd fd, const crypto::Secret& key, key_slot_t slot,
                                   fbl::unique_ptr<FdioVolume>* out) {
    zx_status_t rc;

    fbl::unique_ptr<FdioVolume> volume;
    if ((rc = FdioVolume::Init(std::move(fd), &volume)) != ZX_OK ||
        (rc = volume->Unlock(key, slot)) != ZX_OK) {
        return rc;
    }

    *out = std::move(volume);
    return ZX_OK;
}

zx_status_t FdioVolume::Unlock(const crypto::Secret& key, key_slot_t slot) {
    return Volume::Unlock(key, slot);
}

// Configuration methods
zx_status_t FdioVolume::Enroll(const crypto::Secret& key, key_slot_t slot) {
    zx_status_t rc;

    if ((rc = SealBlock(key, slot)) != ZX_OK || (rc = CommitBlock()) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

zx_status_t FdioVolume::Revoke(key_slot_t slot) {
    zx_status_t rc;

    zx_off_t off;
    crypto::Bytes invalid;
    if ((rc = GetSlotOffset(slot, &off)) != ZX_OK || (rc = invalid.Randomize(slot_len_)) != ZX_OK ||
        (rc = block_.Copy(invalid, off)) != ZX_OK || (rc = CommitBlock()) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

zx_status_t FdioVolume::Init() {
    return Volume::Init();
}

zx_status_t FdioVolume::Open(const zx::duration& timeout, fbl::unique_fd* out) {
    zx_status_t rc;

    fdio_t* io = fdio_unsafe_fd_to_io(fd_.get());
    if (io == nullptr) {
        xprintf("could not convert fd to io\n");
        return ZX_ERR_BAD_STATE;
    }
    auto cleanup_fdio = fbl::MakeAutoCall([io]() {
        fdio_unsafe_release(io);
    });

    // Get the full device path
    fbl::StringBuffer<PATH_MAX> path;
    path.Resize(path.capacity());
    zx_status_t call_status;
    size_t path_len;
    rc = fuchsia_device_ControllerGetTopologicalPath(fdio_unsafe_borrow_channel(io),
                                                     &call_status, path.data(),
                                                     path.capacity(), &path_len);
    if (rc == ZX_OK) {
        rc = call_status;
    }
    if (rc != ZX_OK) {
        xprintf("could not find parent device: %s\n", zx_status_get_string(rc));
        return rc;
    }
    path.Resize(path_len);
    path.Append("/zxcrypt/unsealed/block");

    // Early return if already bound
    fbl::unique_fd fd(open(path.c_str(), O_RDWR));
    if (fd) {
        out->reset(fd.release());
        return ZX_OK;
    }

    // Bind the device
    rc = fuchsia_device_ControllerBind(fdio_unsafe_borrow_channel(io), kDriverLib,
                                       strlen(kDriverLib), &call_status);
    if (rc == ZX_OK) {
        rc = call_status;
    }
    if (rc != ZX_OK) {
        xprintf("could not bind zxcrypt driver: %s\n", zx_status_get_string(rc));
        return rc;
    }
    if ((rc = wait_for_device(path.c_str(), timeout.get())) != ZX_OK) {
        xprintf("zxcrypt driver failed to bind: %s\n", zx_status_get_string(rc));
        return rc;
    }
    fd.reset(open(path.c_str(), O_RDWR));
    if (!fd) {
        xprintf("failed to open zxcrypt volume\n");
        return ZX_ERR_NOT_FOUND;
    }

    out->reset(fd.release());
    return ZX_OK;
}

zx_status_t FdioVolume::Ioctl(int op, const void* in, size_t in_len, void* out, size_t out_len) {
    // Don't include debug messages here; some errors (e.g. ZX_ERR_NOT_SUPPORTED)
    // are expected under certain conditions (e.g. calling FVM ioctls on a non-FVM
    // device).  Handle error reporting at the call sites instead.

    ssize_t res;
    if ((res = fdio_ioctl(fd_.get(), op, in, in_len, out, out_len)) < 0) {
        return static_cast<zx_status_t>(res);
    }

    return ZX_OK;
}

zx_status_t FdioVolume::Read() {
    if (lseek(fd_.get(), offset_, SEEK_SET) < 0) {
        xprintf("lseek(%d, %" PRIu64 ", SEEK_SET) failed: %s\n", fd_.get(), offset_,
                strerror(errno));
        return ZX_ERR_IO;
    }
    ssize_t res;
    if ((res = read(fd_.get(), block_.get(), block_.len())) < 0) {
        xprintf("read(%d, %p, %zu) failed: %s\n", fd_.get(), block_.get(), block_.len(),
                strerror(errno));
        return ZX_ERR_IO;
    }
    if (static_cast<size_t>(res) != block_.len()) {
        xprintf("short read: have %zd, need %zu\n", res, block_.len());
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

zx_status_t FdioVolume::Write() {
    if (lseek(fd_.get(), offset_, SEEK_SET) < 0) {
        xprintf("lseek(%d, %" PRIu64 ", SEEK_SET) failed: %s\n", fd_.get(), offset_,
                strerror(errno));
        return ZX_ERR_IO;
    }
    ssize_t res;
    if ((res = write(fd_.get(), block_.get(), block_.len())) < 0) {
        xprintf("write(%d, %p, %zu) failed: %s\n", fd_.get(), block_.get(), block_.len(),
                strerror(errno));
        return ZX_ERR_IO;
    }
    if (static_cast<size_t>(res) != block_.len()) {
        xprintf("short write: have %zd, need %zu\n", res, block_.len());
        return ZX_ERR_IO;
    }
    return ZX_OK;
}


} // namespace zxcrypt
