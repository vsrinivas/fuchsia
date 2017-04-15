// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>

#include <magenta/device/intel-hda.h>
#include <mxio/io.h>
#include <mxtl/limits.h>

#include "magenta_device.h"

namespace audio {
namespace intel_hda {

uint32_t MagentaDevice::transaction_id_ = 0;

mx_status_t MagentaDevice::Connect() {
    if (dev_channel_ != MX_HANDLE_INVALID)
        return NO_ERROR;

    if (!dev_name_)
        return ERR_NO_MEMORY;

    int fd = ::open(dev_name_, O_RDONLY);
    if (fd < 0)
        return static_cast<mx_status_t>(fd);

    ssize_t res = ::mxio_ioctl(fd, IHDA_IOCTL_GET_CHANNEL,
                               nullptr, 0,
                               &dev_channel_, sizeof(dev_channel_));
    ::close(fd);

    if (res < 0) {
        printf("[%s] Failed to fetch device channel (%zd)\n", dev_name(), res);
        return static_cast<mx_status_t>(res);
    }

    return NO_ERROR;
}

void MagentaDevice::Disconnect() {
    if (dev_channel_ != MX_HANDLE_INVALID) {
        ::mx_handle_close(dev_channel_);
        dev_channel_ = MX_HANDLE_INVALID;
    }
}

mx_status_t MagentaDevice::CallDevice(const mx_channel_call_args_t& args, uint64_t timeout_msec) {
    mx_status_t res;
    mx_status_t read_status;
    uint32_t resp_size;
    uint32_t resp_handles;
    mx_time_t timeout;

    if (timeout_msec == MX_TIME_INFINITE) {
        timeout = MX_TIME_INFINITE;
    } else if (timeout_msec >= mxtl::numeric_limits<mx_time_t>::max() / MX_MSEC(1)) {
        return ERR_INVALID_ARGS;
    } else {
        timeout = MX_MSEC(timeout_msec);
    }

    res = mx_channel_call(dev_channel_, 0, timeout,
                          &args, &resp_size, &resp_handles, &read_status);

    return (res == ERR_CALL_FAILED) ? read_status : res;
}

mx_status_t MagentaDevice::Enumerate(
        void* ctx,
        const char* const dev_path,
        const char* const dev_fmt,
        EnumerateCbk cbk) {
    static constexpr size_t FILENAME_SIZE = 256;

    struct dirent* de;
    DIR* dir = opendir(dev_path);
    mx_status_t res = NO_ERROR;
    char buf[FILENAME_SIZE];

    if (!dir)
        return ERR_NOT_FOUND;

    while ((de = readdir(dir)) != NULL) {
        uint32_t id;
        if (sscanf(de->d_name, dev_fmt, &id) == 1) {
            size_t total = 0;

            total += snprintf(buf + total, sizeof(buf) - total, "%s/", dev_path);
            total += snprintf(buf + total, sizeof(buf) - total, dev_fmt, id);

            mx_status_t res = cbk(ctx, id, buf);
            if (res != NO_ERROR)
                goto done;
        }
    }

done:
    closedir(dir);
    return res;
}

}  // namespace audio
}  // namespace intel_hda
