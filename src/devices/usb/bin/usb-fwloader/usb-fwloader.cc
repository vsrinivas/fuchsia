// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/usb/fwloader/c/fidl.h>
#include <fuchsia/hardware/usb/tester/c/fidl.h>
#include <fuchsia/mem/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/hw/usb.h>
#include <zircon/types.h>

#include <memory>
#include <variant>

#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>

namespace {

struct WatchDirData {
  const char* dev_name;
  fbl::unique_fd fd;
};

enum class Mode {
  kUpdateTest = 0,             // Update the test firmware.
  kUpdateTestBoot = 1,         // Update the test device bootloader.
  kDeviceFirmwareUpgrade = 2,  // Perform a DFU. The device must implement the USB DFU Spec.
};

constexpr char kFwLoaderDir[] = "/dev/class/usb-fwloader";
constexpr char kUsbTesterDevDir[] = "/dev/class/usb-tester";

constexpr char kFirmwareLoader[] = "fx3";
constexpr char kFlashProgrammer[] = "flash-programmer";
constexpr char kUSBDFU[] = "usb-dfu";
constexpr char kUSBTester[] = "usb-tester";

constexpr int kEnumerationWaitSecs = 5;

constexpr uint32_t kBufferSize = 8 * 1024;

void usage(const char* prog_name) {
  printf("usage:\n");
  printf("%s [options]\n", prog_name);
  printf("\nOptions\n");
  printf(
      "  -t                   : Load test firmware mode.\n"
      "                         This is the default if no mode is specified.\n"
      "  -b                   : Flash bootloader mode.\n"
      "  -d                   : USB Device Firmware Upgrade.\n"
      "  -f <firmware_path>   : Firmware to load.\n"
      "  -p <flash_prog_path> : Firmware image for the flash programmer.\n"
      "                         This is only required when flashing a new bootloader.\n");
}

zx_status_t fd_matches_name(const fbl::unique_fd& fd, const char* dev_name, bool* out_match) {
  char path[PATH_MAX];
  fdio_t* io = fdio_unsafe_fd_to_io(fd.get());
  if (io == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  zx_status_t call_status = ZX_OK;
  size_t path_len;
  auto resp = ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(
      zx::unowned_channel(fdio_unsafe_borrow_channel(io)));
  zx_status_t status = resp.status();
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  } else {
    path_len = resp->result.response().path.size();
    auto& r = resp->result.response();
    memcpy(path, r.path.data(), r.path.size());
  }

  fdio_unsafe_release(io);
  if (status != ZX_OK || call_status != ZX_OK) {
    return ZX_ERR_IO;
  }
  path[path_len] = 0;

  *out_match = dev_name == nullptr || strstr(path, dev_name) != nullptr;
  return ZX_OK;
}

zx_status_t watch_dir_cb(int dirfd, int event, const char* filename, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  fbl::unique_fd fd(openat(dirfd, filename, O_RDWR));
  if (!fd) {
    return ZX_OK;
  }
  auto data = reinterpret_cast<WatchDirData*>(cookie);
  bool match = false;
  zx_status_t status = fd_matches_name(fd, data->dev_name, &match);
  if (status != ZX_OK || !match) {
    return status;
  }
  data->fd = std::move(fd);
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
  WatchDirData data = {.dev_name = dev_name, .fd = fbl::unique_fd()};
  zx_status_t status =
      fdio_watch_directory(dirfd(d), watch_dir_cb, zx_deadline_after(ZX_SEC(kEnumerationWaitSecs)),
                           reinterpret_cast<void*>(&data));
  if (status == ZX_ERR_STOP) {
    *out_fd = std::move(data.fd);
    return ZX_OK;
  } else {
    return status;
  }
}

zx_status_t open_dev(const char* dir, const char* dev_name, fbl::unique_fd* out_fd) {
  DIR* d = opendir(dir);
  if (d == nullptr) {
    fprintf(stderr, "Could not open dir: \"%s\"\n", dir);
    return ZX_ERR_BAD_STATE;
  }

  struct dirent* de;
  while ((de = readdir(d)) != nullptr) {
    fbl::unique_fd fd(openat(dirfd(d), de->d_name, O_RDWR));
    if (!fd) {
      continue;
    }
    bool match = false;
    zx_status_t status = fd_matches_name(fd, dev_name, &match);
    if (status != ZX_OK || !match) {
      continue;
    }
    *out_fd = std::move(fd);
    closedir(d);
    return ZX_OK;
  }
  closedir(d);

  return ZX_ERR_NOT_FOUND;
}

zx_status_t open_test_fwloader_dev(fbl::unique_fd* out_fd) {
  return open_dev(kFwLoaderDir, kFirmwareLoader, out_fd);
}

zx_status_t open_usb_tester_dev(fbl::unique_fd* out_fd) {
  return open_dev(kUsbTesterDevDir, kUSBTester, out_fd);
}

zx_status_t open_dfu_dev(fbl::unique_fd* out_fd) { return open_dev(kFwLoaderDir, kUSBDFU, out_fd); }

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

  std::unique_ptr<unsigned char[]> buf(new unsigned char[kBufferSize]);
  ssize_t res;
  off_t total_read = 0;
  while ((total_read < s.st_size) && ((res = read(file_fd.get(), buf.get(), kBufferSize)) != 0)) {
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

// Loads the given firmware onto the device.
// |firmware| should either be the firmware file path, or a prebuilt type.
zx_status_t device_load_firmware(
    fbl::unique_fd fd,
    std::variant<const char*, fuchsia_hardware_usb_fwloader_PrebuiltType> firmware) {
  zx::channel svc;
  zx_status_t status = fdio_get_service_handle(fd.release(), svc.reset_and_get_address());
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to get fwloader service handle, err : %d\n", status);
    return status;
  }

  zx::vmo fw_vmo;
  size_t fw_size = 0;
  if (auto firmware_path = std::get_if<const char*>(&firmware)) {
    fbl::unique_fd file_fd(open(*firmware_path, O_RDONLY));
    if (!file_fd) {
      fprintf(stderr, "Failed to open \"%s\", err: %s\n", *firmware_path, strerror(errno));
      return ZX_ERR_IO;
    }
    zx_status_t status = read_firmware(file_fd, fw_vmo, &fw_size);
    if (status != ZX_OK) {
      fprintf(stderr, "Failed to read firmware file, err: %d\n", status);
      return status;
    }
    fuchsia_mem_Buffer firmware = {.vmo = fw_vmo.release(), .size = fw_size};
    zx_status_t res =
        fuchsia_hardware_usb_fwloader_DeviceLoadFirmware(svc.get(), &firmware, &status);
    if (res == ZX_OK) {
      res = status;
    }
    if (res != ZX_OK) {
      fprintf(stderr, "Failed to load firmware, err: %d\n", res);
      return res;
    }
  } else if (auto type = std::get_if<fuchsia_hardware_usb_fwloader_PrebuiltType>(&firmware)) {
    zx_status_t status;
    zx_status_t res =
        fuchsia_hardware_usb_fwloader_DeviceLoadPrebuiltFirmware(svc.get(), *type, &status);
    if (res == ZX_OK) {
      res = status;
    }
    if (res != ZX_OK) {
      fprintf(stderr, "Failed to load prebuilt firmware, err: %d\n", res);
      return res;
    }
  } else {
    fprintf(stderr, "Firmware not specified\n");
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

// Loads the firmware image to the FX3 device RAM.
// |firmware| should either be the firmware file path, or a prebuilt type.
zx_status_t load_to_ram(
    const char* firmware_path,
    std::variant<const char*, fuchsia_hardware_usb_fwloader_PrebuiltType> firmware) {
  fbl::unique_fd fd;
  zx_status_t status = open_test_fwloader_dev(&fd);
  if (status != ZX_OK) {
    fbl::unique_fd usb_tester_fd;
    // Check if there is a usb tester device we can switch to firmware loading mode.
    status = open_usb_tester_dev(&usb_tester_fd);
    if (status != ZX_OK) {
      fprintf(stderr, "No usb test fwloader or tester device found, err: %d\n", status);
      return status;
    }
    zx::channel usb_tester_svc;
    status =
        fdio_get_service_handle(usb_tester_fd.release(), usb_tester_svc.reset_and_get_address());
    if (status != ZX_OK) {
      fprintf(stderr, "Failed to get usb tester device service handle, err : %d\n", status);
      return status;
    }
    printf("Switching usb tester device to fwloader mode\n");
    zx_status_t res =
        fuchsia_hardware_usb_tester_DeviceSetModeFwloader(usb_tester_svc.get(), &status);
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
  return device_load_firmware(std::move(fd), firmware);
}

zx_status_t load_test_firmware(const char* firmware_path) {
  std::variant<const char*, fuchsia_hardware_usb_fwloader_PrebuiltType> firmware;
  if (firmware_path) {
    firmware = firmware_path;
  } else {
    firmware = static_cast<fuchsia_hardware_usb_fwloader_PrebuiltType>(
        fuchsia_hardware_usb_fwloader_PrebuiltType_TESTER);
  }
  zx_status_t status = load_to_ram(firmware_path, firmware);
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
  status = fuchsia_hardware_usb_tester_DeviceGetVersion(svc.get(), &major_version, &minor_version);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to get updated device version, err: %d\n", status);
    return status;
  }
  printf("Updated usb tester firmware to v%x.%x\n", major_version, minor_version);
  return ZX_OK;
}

zx_status_t load_bootloader(const char* flash_prog_image_path, const char* firmware_path) {
  std::variant<const char*, fuchsia_hardware_usb_fwloader_PrebuiltType> firmware;
  if (flash_prog_image_path) {
    firmware = flash_prog_image_path;
  } else {
    firmware = static_cast<fuchsia_hardware_usb_fwloader_PrebuiltType>(
        fuchsia_hardware_usb_fwloader_PrebuiltType_FLASH);
  }

  zx_status_t status = load_to_ram(flash_prog_image_path, firmware);
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
  if (firmware_path) {
    firmware = firmware_path;
  } else {
    firmware = static_cast<fuchsia_hardware_usb_fwloader_PrebuiltType>(
        fuchsia_hardware_usb_fwloader_PrebuiltType_BOOT);
  }
  status = device_load_firmware(std::move(updated_dev), firmware);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to write bootloader, err: %d\n", status);
    return status;
  }
  printf("Updated bootloader.\n");
  return ZX_OK;
}

zx_status_t device_firmware_upgrade(const char* firmware_path) {
  fbl::unique_fd fd;
  zx_status_t status = open_dfu_dev(&fd);
  if (status != ZX_OK) {
    fprintf(stderr, "Could not find any connected USB DFU device.\n");
    return status;
  }
  // TODO(jocelyndang): support prebuilts.
  std::variant<const char*, fuchsia_hardware_usb_fwloader_PrebuiltType> firmware = firmware_path;
  status = device_load_firmware(std::move(fd), firmware);
  if (status != ZX_OK) {
    fprintf(stderr, "Device firmware upgrade failed, err: %d\n", status);
    return status;
  }
  printf("Finished device firmware upgrade.\n");
  return ZX_OK;
}

}  // namespace

int main(int argc, char** argv) {
  auto print_usage = fbl::MakeAutoCall([prog_name = argv[0]]() { usage(prog_name); });

  Mode mode = Mode::kUpdateTest;
  const char* firmware_path = nullptr;
  const char* flash_prog_path = nullptr;

  int opt;
  while ((opt = getopt(argc, argv, "tbdf:p:")) != -1) {
    switch (opt) {
      case 't':
        mode = Mode::kUpdateTest;
        break;
      case 'b':
        mode = Mode::kUpdateTestBoot;
        break;
      case 'd':
        mode = Mode::kDeviceFirmwareUpgrade;
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
  print_usage.cancel();

  zx_status_t status;
  switch (mode) {
    case Mode::kUpdateTest:
      status = load_test_firmware(firmware_path);
      break;
    case Mode::kUpdateTestBoot:
      status = load_bootloader(flash_prog_path, firmware_path);
      break;
    case Mode::kDeviceFirmwareUpgrade:
      status = device_firmware_upgrade(firmware_path);
      break;
  }
  return status == ZX_OK ? 0 : -1;
}
