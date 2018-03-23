// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>

#include <zircon/device/intel-hda.h>
#include <fdio/io.h>
#include <fbl/limits.h>

#include "zircon_device.h"

namespace audio {
namespace intel_hda {

uint32_t ZirconDevice::transaction_id_ = 0;

zx_status_t ZirconDevice::Connect() {
    if (dev_channel_ != ZX_HANDLE_INVALID)
        return ZX_OK;

    if (!dev_name_)
        return ZX_ERR_NO_MEMORY;

    int fd = ::open(dev_name_, O_RDONLY);
    if (fd < 0)
        return static_cast<zx_status_t>(fd);

    ssize_t res = ::fdio_ioctl(fd, IHDA_IOCTL_GET_CHANNEL,
                               nullptr, 0,
                               &dev_channel_, sizeof(dev_channel_));
    ::close(fd);

    if (res < 0) {
        printf("[%s] Failed to fetch device channel (%zd)\n", dev_name(), res);
        return static_cast<zx_status_t>(res);
    }

    return ZX_OK;
}

void ZirconDevice::Disconnect() {
    if (dev_channel_ != ZX_HANDLE_INVALID) {
        ::zx_handle_close(dev_channel_);
        dev_channel_ = ZX_HANDLE_INVALID;
    }
}

zx_status_t ZirconDevice::CallDevice(const zx_channel_call_args_t& args, uint64_t timeout_msec) {
    zx_status_t res;
    zx_status_t read_status;
    uint32_t resp_size;
    uint32_t resp_handles;
    zx_time_t deadline;

    if (timeout_msec == ZX_TIME_INFINITE) {
        deadline = ZX_TIME_INFINITE;
    } else if (timeout_msec >= fbl::numeric_limits<zx_time_t>::max() / ZX_MSEC(1)) {
        return ZX_ERR_INVALID_ARGS;
    } else {
        deadline = zx_deadline_after(ZX_MSEC(timeout_msec));
    }

    res = zx_channel_call(dev_channel_, 0, deadline,
                          &args, &resp_size, &resp_handles, &read_status);

    return (res == ZX_ERR_CALL_FAILED) ? read_status : res;
}

zx_status_t ZirconDevice::Enumerate(
        void* ctx,
        const char* const dev_path,
        EnumerateCbk cbk) {
    static constexpr size_t FILENAME_SIZE = 256;

    struct dirent* de;
    DIR* dir = opendir(dev_path);
    zx_status_t res = ZX_OK;
    char buf[FILENAME_SIZE];

    if (!dir)
        return ZX_ERR_NOT_FOUND;

    while ((de = readdir(dir)) != NULL) {
        uint32_t id;
        if (sscanf(de->d_name, "%u", &id) == 1) {
            size_t total = 0;

            total += snprintf(buf + total, sizeof(buf) - total, "%s/", dev_path);
            total += snprintf(buf + total, sizeof(buf) - total, "%03u", id);

            res = cbk(ctx, id, buf);
            if (res != ZX_OK)
                goto done;
        }
    }

done:
    closedir(dir);
    return res;
}

}  // namespace audio
}  // namespace intel_hda
