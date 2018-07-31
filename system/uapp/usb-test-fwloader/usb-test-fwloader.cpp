// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/vmo.h>
#include <zircon/device/usb-test-fwloader.h>
#include <zircon/types.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char* const DEV_DIR = "/dev/class/usb-test-fwloader";
static constexpr uint32_t BUFFER_SIZE = 8 * 1024;

zx_status_t open_fwloader_dev(fbl::unique_fd& out_fd) {
    DIR* d = opendir(DEV_DIR);
    if (d == nullptr) {
        fprintf(stderr, "Could not open dir: \"%s\"\n", DEV_DIR);
        return ZX_ERR_BAD_STATE;
    }

    struct dirent* de;
    while ((de = readdir(d)) != nullptr) {
        int fd = openat(dirfd(d), de->d_name, O_RDWR);
        if (fd < 0) {
            continue;
        }
        out_fd.reset(fd);
        closedir(d);
        return ZX_OK;
    }
    closedir(d);

    fprintf(stderr, "No test fwloader device found\n");
    return ZX_ERR_NOT_FOUND;
}

// Reads the firmware file and populates the provided vmo with the contents.
static zx_status_t read_firmware(fbl::unique_fd& file_fd, zx::vmo& vmo) {
    struct stat s;
    if (fstat(file_fd.get(), &s) < 0) {
        fprintf(stderr, "could not get size of file, err: %s\n", strerror(errno));
        return ZX_ERR_IO;
    }
    zx_status_t status = zx::vmo::create(s.st_size, 0, &vmo);
    if (status != ZX_OK) {
        return status;
    }

    fbl::unique_ptr<unsigned char[]> buf(new unsigned char[BUFFER_SIZE]);
    ssize_t res;
    off_t total_read = 0;
    while ((total_read < s.st_size) &&
           ((res = read(file_fd.get(), buf.get(), BUFFER_SIZE)) != 0)) {
        if (res < 0) {
            fprintf(stderr, "Fatal read error: %s\n", strerror(errno));
            return ZX_ERR_IO;
        }
        zx_status_t status = vmo.write(buf.get(), total_read, res);
        if (status != ZX_OK) {
            return status;
        }
        total_read += res;
    }
    if (total_read != s.st_size) {
        fprintf(stderr, "Read %jd bytes, want %jd\n", (intmax_t)total_read, (intmax_t)s.st_size);
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("Usage: %s <firmware_image_path>\n", argv[0]);
        return -1;
    }
    const char* filename = argv[1];
    fbl::unique_fd file_fd(open(filename, O_RDONLY));
    if (!file_fd) {
        fprintf(stderr, "Failed to open \"%s\", err: %s\n", filename, strerror(errno));
        return -1;
    }
    zx::vmo fw_vmo;
    zx_status_t status = read_firmware(file_fd, fw_vmo);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to read firmware file, err: %d\n", status);
        return -1;
    }
    fbl::unique_fd fd;
    status = open_fwloader_dev(fd);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to open device, err: %d\n", status);
        return -1;
    }
    zx_handle_t handle = fw_vmo.release();
    ssize_t res = ioctl_usb_test_fwloader_load_firmware(fd.get(), &handle);
    if (res < ZX_OK) {
        fprintf(stderr, "Failed to load firmware, err: %zd\n", res);
        return -1;
    }
    return 0;
}
