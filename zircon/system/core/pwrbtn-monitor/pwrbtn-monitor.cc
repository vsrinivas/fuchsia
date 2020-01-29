// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <fuchsia/hardware/input/c/fidl.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/channel.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <ddk/device.h>

#include <utility>

#define INPUT_PATH "/input"

namespace {

bool usage_eq(const hid::Usage& u1, const hid::Usage& u2) {
  return u1.page == u2.page && u1.usage == u2.usage;
}

// Search the report descriptor for a System Power Down input field within a
// Generic Desktop:System Control collection.
//
// This method assumes the HID descriptor does not contain more than one such field.
zx_status_t FindSystemPowerDown(const hid::DeviceDescriptor* desc, uint8_t* report_id,
                                size_t* bit_offset) {
  const hid::Usage system_control = {
      .page = static_cast<uint16_t>(hid::usage::Page::kGenericDesktop),
      .usage = static_cast<uint32_t>(hid::usage::GenericDesktop::kSystemControl),
  };

  const hid::Usage power_down = {
      .page = static_cast<uint16_t>(hid::usage::Page::kGenericDesktop),
      .usage = static_cast<uint32_t>(hid::usage::GenericDesktop::kSystemPowerDown),
  };

  // Search for the field
  for (size_t rpt_idx = 0; rpt_idx < desc->rep_count; ++rpt_idx) {
    const hid::ReportDescriptor& report = desc->report[rpt_idx];

    for (size_t i = 0; i < report.input_count; ++i) {
      const hid::ReportField& field = report.input_fields[i];

      if (!usage_eq(field.attr.usage, power_down)) {
        continue;
      }

      const hid::Collection* collection = hid::GetAppCollection(&field);
      if (!collection || !usage_eq(collection->usage, system_control)) {
        continue;
      }
      *report_id = field.report_id;
      *bit_offset = field.attr.offset;
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

struct PowerButtonInfo {
  fbl::unique_fd fd;
  uint8_t report_id;
  size_t bit_offset;
  bool has_report_id_byte;
};

static zx_status_t InputDeviceAdded(int dirfd, int event, const char* name, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }

  fbl::unique_fd fd;
  {
    int raw_fd;
    if ((raw_fd = openat(dirfd, name, O_RDWR)) < 0) {
      return ZX_OK;
    }
    fd.reset(raw_fd);
  }
  fdio_cpp::FdioCaller caller(std::move(fd));

  // Retrieve and parse the report descriptor
  uint16_t desc_len = 0;
  zx_status_t status =
      fuchsia_hardware_input_DeviceGetReportDescSize(caller.borrow_channel(), &desc_len);
  if (status != ZX_OK) {
    return ZX_OK;
  }
  if (desc_len > fuchsia_hardware_input_MAX_DESC_LEN) {
    return ZX_OK;
  }

  fbl::AllocChecker ac;
  fbl::Array<uint8_t> raw_desc(new (&ac) uint8_t[desc_len](), desc_len);
  if (!ac.check()) {
    return ZX_OK;
  }

  size_t actual_size;
  status = fuchsia_hardware_input_DeviceGetReportDesc(caller.borrow_channel(), raw_desc.data(),
                                                      raw_desc.size(), &actual_size);
  if (status != ZX_OK || actual_size != raw_desc.size()) {
    return ZX_OK;
  }

  hid::DeviceDescriptor* desc;
  if (hid::ParseReportDescriptor(raw_desc.data(), raw_desc.size(), &desc) != hid::kParseOk) {
    return ZX_OK;
  }
  auto cleanup_desc = fbl::MakeAutoCall([desc]() { hid::FreeDeviceDescriptor(desc); });

  uint8_t report_id;
  size_t bit_offset;
  status = FindSystemPowerDown(desc, &report_id, &bit_offset);
  if (status != ZX_OK) {
    return ZX_OK;
  }

  auto info = reinterpret_cast<PowerButtonInfo*>(cookie);
  info->fd = caller.release();
  info->report_id = report_id;
  info->bit_offset = bit_offset;
  info->has_report_id_byte = (desc->rep_count > 1 || desc->report[0].report_id != 0);
  return ZX_ERR_STOP;
}

zx_status_t send_poweroff() {
  zx::channel channel_local, channel_remote;
  zx_status_t status = zx::channel::create(0, &channel_local, &channel_remote);
  if (status != ZX_OK) {
    printf("failed to create channel: %d\n", status);
    return ZX_ERR_INTERNAL;
  }

  const char* service = "/svc/" fuchsia_device_manager_Administrator_Name;
  status = fdio_service_connect(service, channel_remote.get());
  if (status != ZX_OK) {
    fprintf(stderr, "failed to connect to service %s: %d\n", service, status);
    return ZX_ERR_INTERNAL;
  }

  zx_status_t call_status;
  status = fuchsia_device_manager_AdministratorSuspend(channel_local.get(),
                                                       DEVICE_SUSPEND_FLAG_POWEROFF, &call_status);
  if (status != ZX_OK || call_status != ZX_OK) {
    fprintf(stderr, "Call to %s failed: ret: %d  remote: %d\n", service, status, call_status);
    return status != ZX_OK ? status : call_status;
  }

  return ZX_OK;
}

}  // namespace

int main(int argc, char** argv) {
  fbl::unique_fd dirfd;
  {
    int fd = open(INPUT_PATH, O_DIRECTORY | O_RDONLY);
    if (fd < 0) {
      printf("pwrbtn-monitor: Failed to open " INPUT_PATH ": %d\n", errno);
      return 1;
    }
    dirfd.reset(fd);
  }

  PowerButtonInfo info;
  zx_status_t status = fdio_watch_directory(dirfd.get(), InputDeviceAdded, ZX_TIME_INFINITE, &info);
  if (status != ZX_ERR_STOP) {
    printf("pwrbtn-monitor: Failed to find power button device\n");
    return 1;
  }
  dirfd.reset();

  fdio_cpp::FdioCaller caller(std::move(info.fd));
  uint16_t report_size = 0;
  if (fuchsia_hardware_input_DeviceGetMaxInputReportSize(caller.borrow_channel(), &report_size) !=
      ZX_OK) {
    printf("pwrbtn-monitor: Failed to to get max report size\n");
    return 1;
  }

  // Double-check the size looks right
  const size_t byte_index = info.has_report_id_byte + info.bit_offset / 8;
  if (report_size <= byte_index) {
    printf("pwrbtn-monitor: Suspicious looking max report size\n");
    return 1;
  }

  fbl::AllocChecker ac;
  fbl::Array<uint8_t> report(new (&ac) uint8_t[report_size](), report_size);
  if (!ac.check()) {
    return 1;
  }

  info.fd = caller.release();

  // Watch the power button device for reports
  while (true) {
    ssize_t r = read(info.fd.get(), report.data(), report.size());
    if (r < 0) {
      printf("pwrbtn-monitor: got read error %zd, bailing\n", r);
      return 1;
    }

    // Ignore reports from different report IDs
    if (info.has_report_id_byte && report[0] != info.report_id) {
      printf("pwrbtn-monitor: input-watcher: wrong id\n");
      continue;
    }

    if (static_cast<size_t>(r) <= byte_index) {
      printf("pwrbtn-monitor: input-watcher: too short\n");
      continue;
    }

    // Check if the power button is pressed, and request a poweroff if so.
    if (report[byte_index] & (1u << (info.bit_offset % 8))) {
      auto status = send_poweroff();
      if (status != ZX_OK) {
        printf("pwrbtn-monitor: input-watcher: failed send poweroff to device manager.\n");
        continue;
      }
    }
  }
}
