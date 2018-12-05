// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/mem/c/fidl.h>
#include <lib/fdio/util.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zircon/device/device.h>
#include <zircon/hw/usb.h>
#include <zircon/types.h>
#include <zircon/usb/test/fwloader/c/fidl.h>
#include <zircon/usb/tester/c/fidl.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

namespace {

struct WatchDirData {
    const char* dev_name;
    int fd;
};

constexpr char kFwLoaderDir[] = "/dev/class/usb-test-fwloader";
constexpr char kUsbTesterDevDir[] = "/dev/class/usb-tester";

constexpr char kFirmwareLoader[] = "fx3";
constexpr char kFlashProgrammer[] = "flash-programmer";

constexpr int kEnumerationWaitSecs = 5;

constexpr uint32_t kBufferSize = 8 * 1024;

void usage(const char* prog_name) {
    printf("usage:\n");
    printf("%s [options]\n", prog_name);
    printf("\nOptions\n");
    printf("  -t                   : Load test firmware mode.\n"
           "                         This is the default if no mode is specified.\n"
           "  -b                   : Flash bootloader mode.\n"
           "  -f <firmware_path>   : Firmware to load.\n"
           "  -p <flash_prog_path> : Firmware image for the flash programmer.\n"
           "                         This is required when flashing a new bootloader.\n");
}

zx_status_t watch_dir_cb(int dirfd, int event, const char* filename, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        return ZX_OK;
    }
    int fd = openat(dirfd, filename, O_RDWR);
    if (fd < 0) {
        return ZX_OK;
    }
    auto data = reinterpret_cast<WatchDirData*>(cookie);
    char path[PATH_MAX];
    const ssize_t r = ioctl_device_get_topo_path(fd, path, sizeof(path));
    if (r < 0) {
        return ZX_ERR_IO;
    }
    if (data->dev_name && strstr(path, data->dev_name) == nullptr) {
        close(fd);
        return ZX_OK;
    }
    data->fd = fd;
    return ZX_ERR_STOP;
}

// Waits for a device to enumerate and be added to the given directory.
zx_status_t wait_dev_enumerate(const char* dir, const char* dev_name, fbl::unique_fd* out_fd) {
    DIR* d = opendir(dir);
    if (d == nullptr) {
        fprintf(stderr, "Could not open dir: \"%s\"\n", dir);
        return ZX_ERR_BAD_STATE;
    }
    auto close_dir = fbl::MakeAutoCall([&] { closedir(d); });
    WatchDirData data = { .dev_name = dev_name, .fd = 0 };
    zx_status_t status = fdio_watch_directory(dirfd(d), watch_dir_cb,
                                              zx_deadline_after(ZX_SEC(kEnumerationWaitSecs)),
                                              reinterpret_cast<void*>(&data));
    if (status == ZX_ERR_STOP) {
        out_fd->reset(data.fd);
        return ZX_OK;
    } else {
        return status;
    }
}

zx_status_t open_dev(const char* dir, fbl::unique_fd* out_fd) {
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
        out_fd->reset(fd);
        closedir(d);
        return ZX_OK;
    }
    closedir(d);

    return ZX_ERR_NOT_FOUND;
}

zx_status_t open_fwloader_dev(fbl::unique_fd* out_fd) {
    return open_dev(kFwLoaderDir, out_fd);
}

zx_status_t open_usb_tester_dev(fbl::unique_fd* out_fd) {
    return open_dev(kUsbTesterDevDir, out_fd);
}
// Reads the firmware file and populates the provided vmo with the contents.
zx_status_t read_firmware(fbl::unique_fd& file_fd, zx::vmo& vmo, size_t* out_fw_size) {
    struct stat s;
    if (fstat(file_fd.get(), &s) < 0) {
        fprintf(stderr, "could not get size of file, err: %s\n", strerror(errno));
        return ZX_ERR_IO;
    }
    zx_status_t status = zx::vmo::create(s.st_size, 0, &vmo);
    if (status != ZX_OK) {
        return status;
    }

    fbl::unique_ptr<unsigned char[]> buf(new unsigned char[kBufferSize]);
    ssize_t res;
    off_t total_read = 0;
    while ((total_read < s.st_size) &&
           ((res = read(file_fd.get(), buf.get(), kBufferSize)) != 0)) {
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
    *out_fw_size = total_read;
    return ZX_OK;
}

zx_status_t device_load_firmware(fbl::unique_fd fd, const char* firmware_path) {
    zx::vmo fw_vmo;
    size_t fw_size = 0;
    if (firmware_path) {
        fbl::unique_fd file_fd(open(firmware_path, O_RDONLY));
        if (!file_fd) {
            fprintf(stderr, "Failed to open \"%s\", err: %s\n", firmware_path, strerror(errno));
            return ZX_ERR_IO;
        }
        zx_status_t status = read_firmware(file_fd, fw_vmo, &fw_size);
        if (status != ZX_OK) {
            fprintf(stderr, "Failed to read firmware file, err: %d\n", status);
            return status;
        }
    }
    zx::channel svc;
    zx_status_t status = fdio_get_service_handle(fd.release(), svc.reset_and_get_address());
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to get fwloader service handle, err : %d\n", status);
        return status;
    }
    if (fw_vmo.is_valid()) {
        fuchsia_mem_Buffer firmware = { .vmo = fw_vmo.release(), .size = fw_size };
        zx_status_t status;
        zx_status_t res = zircon_usb_test_fwloader_DeviceLoadFirmware(svc.get(), &firmware,
                                                                      &status);
        if (res == ZX_OK) {
            res = status;
        }
        if (res != ZX_OK) {
            fprintf(stderr, "Failed to load firmware, err: %d\n", res);
            return res;
        }
    } else {
        zx_status_t status;
        zx_status_t res = zircon_usb_test_fwloader_DeviceLoadPrebuiltFirmware(svc.get(), &status);
        if (res == ZX_OK) {
            res = status;
        }
        if (res != ZX_OK) {
            fprintf(stderr, "Failed to load prebuilt firmware, err: %d\n", res);
            return res;
        }
    }
    return ZX_OK;
}

// Loads the firmware image to the FX3 device RAM.
zx_status_t load_to_ram(const char* firmware_path) {
    fbl::unique_fd fd;
    zx_status_t status = open_fwloader_dev(&fd);
    if (status != ZX_OK) {
        fbl::unique_fd usb_tester_fd;
        // Check if there is a usb tester device we can switch to firmware loading mode.
        status = open_usb_tester_dev(&usb_tester_fd);
        if (status != ZX_OK) {
            fprintf(stderr, "No usb test fwloader or tester device found, err: %d\n", status);
            return status;
        }
        zx::channel usb_tester_svc;
        status = fdio_get_service_handle(usb_tester_fd.release(),
                                         usb_tester_svc.reset_and_get_address());
        if (status != ZX_OK) {
            fprintf(stderr, "Failed to get usb tester device service handle, err : %d\n", status);
            return status;
        }
        printf("Switching usb tester device to fwloader mode\n");
        zx_status_t res = zircon_usb_tester_DeviceSetModeFwloader(usb_tester_svc.get(), &status);
        if (res == ZX_OK) {
            res = status;
        }
        if (res != ZX_OK) {
            fprintf(stderr, "Failed to switch usb test device to fwloader mode, err: %d\n", res);
            return res;
        }
        status = wait_dev_enumerate(kFwLoaderDir, kFirmwareLoader, &fd);
        if (status != ZX_OK) {
            fprintf(stderr, "Failed to wait for fwloader to re-enumerate, err: %d\n", status);
            return status;
        }
    }
    return device_load_firmware(std::move(fd), firmware_path);
}

zx_status_t load_test_firmware(const char* firmware_path) {
    zx_status_t status = load_to_ram(firmware_path);
    if (status != ZX_OK) {
        return status;
    }
    fbl::unique_fd updated_dev;
    status = wait_dev_enumerate(kUsbTesterDevDir, nullptr, &updated_dev);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to wait for updated usb tester to enumerate, err: %d\n", status);
        return status;
    }
    zx::channel svc;
    status = fdio_get_service_handle(updated_dev.release(), svc.reset_and_get_address());
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to get updated device service handle, err : %d\n", status);
        return status;
    }
    uint8_t major_version;
    uint8_t minor_version;
    status = zircon_usb_tester_DeviceGetVersion(svc.get(), &major_version, &minor_version);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to get updated device version, err: %d\n", status);
        return status;
    }
    printf("Updated usb tester firmware to v%x.%x\n", major_version, minor_version);
    return ZX_OK;
}

zx_status_t load_bootloader(const char* flash_prog_image_path, const char* firmware_path) {
    zx_status_t status = load_to_ram(flash_prog_image_path);
    if (status != ZX_OK) {
        return status;
    }
    fbl::unique_fd updated_dev;
    status = wait_dev_enumerate(kFwLoaderDir, kFlashProgrammer, &updated_dev);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to wait for flash programmer to enumerate, err: %d\n", status);
        return status;
    }
    printf("Loaded flash programmer.\n");
    printf("Loading bootloader to device...\n");
    status = device_load_firmware(std::move(updated_dev), firmware_path);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to write bootloader, err: %d\n", status);
        return status;
    }
    printf("Updated bootloader.\n");
    return ZX_OK;
}

}  // namespace

int main(int argc, char** argv) {
    auto print_usage = fbl::MakeAutoCall([prog_name = argv[0]]() { usage(prog_name); });

    bool load_test_firmware_mode = true;
    const char* firmware_path = nullptr;
    const char* flash_prog_path = nullptr;

    int opt;
    while ((opt = getopt(argc, argv, "tbf:p:")) != -1) {
        switch (opt) {
        case 't':
            load_test_firmware_mode = true;
            break;
        case 'b':
            load_test_firmware_mode = false;
            break;
        case 'f':
            firmware_path = optarg;
            break;
        case 'p':
            flash_prog_path = optarg;
            break;
        default:
            fprintf(stderr, "Invalid option\n");
            return -1;
        }
    }

    // TODO(jocelyndang): for now we require the user specify both files, but we should
    // be able to load them automatically instead.
    if (!load_test_firmware_mode && (!flash_prog_path || !firmware_path)) {
        fprintf(stderr, "Missing flash programmer or bootloader image.\n");
        return -1;
    }

    print_usage.cancel();

    zx_status_t status;
    if (load_test_firmware_mode) {
        status = load_test_firmware(firmware_path);
    } else {
        status = load_bootloader(flash_prog_path, firmware_path);
    }
    return status == ZX_OK ? 0 : -1;
}
