// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/vmo.h>
#include <zircon/device/usb-test-fwloader.h>
#include <zircon/device/usb-tester.h>
#include <zircon/types.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char* const DEV_FX3_DIR = "/dev/class/usb-test-fwloader";
static const char* const DEV_USB_TESTER_DIR = "/dev/class/usb-tester";

static const int ENUMERATION_WAIT_SECS = 5;

static constexpr uint32_t BUFFER_SIZE = 8 * 1024;

zx_status_t watch_dir_cb(int dirfd, int event, const char* filename, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        return ZX_OK;
    }
    int fd = openat(dirfd, filename, O_RDWR);
    if (fd < 0) {
        return ZX_OK;
    }
    auto out_fd = reinterpret_cast<int*>(cookie);
    *out_fd = fd;
    return ZX_ERR_STOP;
}

// Waits for a device to enumerate and be added to the given directory.
zx_status_t wait_dev_enumerate(const char* dir, fbl::unique_fd& out_fd) {
    DIR* d = opendir(dir);
    if (d == nullptr) {
        fprintf(stderr, "Could not open dir: \"%s\"\n", dir);
        return ZX_ERR_BAD_STATE;
    }
    auto close_dir = fbl::MakeAutoCall([&] { closedir(d); });
    int fd = 0;
    zx_status_t status = fdio_watch_directory(dirfd(d), watch_dir_cb,
                                              zx_deadline_after(ZX_SEC(ENUMERATION_WAIT_SECS)),
                                              reinterpret_cast<void*>(&fd));
    if (status == ZX_ERR_STOP) {
        out_fd.reset(fd);
        return ZX_OK;
    } else {
        return status;
    }
}

zx_status_t open_dev(const char* dir, fbl::unique_fd& out_fd) {
    DIR* d = opendir(dir);
    if (d == nullptr) {
        fprintf(stderr, "Could not open dir: \"%s\"\n", dir);
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

    return ZX_ERR_NOT_FOUND;
}

zx_status_t open_fwloader_dev(fbl::unique_fd& out_fd) {
    return open_dev(DEV_FX3_DIR, out_fd);
}

zx_status_t open_usb_tester_dev(fbl::unique_fd& out_fd) {
    return open_dev(DEV_USB_TESTER_DIR, out_fd);
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
        // Check if there is a usb tester device we can switch to firmware loading mode.
        status = open_usb_tester_dev(fd);
        if (status != ZX_OK) {
            fprintf(stderr, "No usb test fwloader or tester device found, err: %d\n", status);
            return -1;
        }
        printf("Switching usb tester device to fwloader mode\n");
        ssize_t res = ioctl_usb_tester_set_mode_fwloader(fd.get());
        if (res != 0) {
            fprintf(stderr, "Failed to switch usb test device to fwloader mode, err: %zd\n", res);
            return -1;
        }
        status = wait_dev_enumerate(DEV_FX3_DIR, fd);
        if (status != ZX_OK) {
            fprintf(stderr, "Failed to wait for fwloader to re-enumerate, err: %d\n", status);
            return -1;
        }
    }
    zx_handle_t handle = fw_vmo.release();
    ssize_t res = ioctl_usb_test_fwloader_load_firmware(fd.get(), &handle);
    if (res < ZX_OK) {
        fprintf(stderr, "Failed to load firmware, err: %zd\n", res);
        return -1;
    }
    return 0;
}
