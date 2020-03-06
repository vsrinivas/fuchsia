// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <fuchsia/hardware/input/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <utility>

#include <ddk/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>

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
  std::optional<llcpp::fuchsia::hardware::input::Device::SyncClient> client;
  uint8_t report_id;
  size_t bit_offset;
  bool has_report_id_byte;
};

static zx_status_t InputDeviceAdded(int dirfd, int event, const char* name, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }

  // Open the fd and get a FIDL client.
  int fd;
  if ((fd = openat(dirfd, name, O_RDWR)) < 0) {
    return ZX_OK;
  }
  zx::channel chan;
  zx_status_t status = fdio_get_service_handle(fd, chan.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }
  auto client = llcpp::fuchsia::hardware::input::Device::SyncClient(std::move(chan));

  // Get the report descriptor.
  auto result = client.GetReportDesc();
  if (result.status() != ZX_OK) {
    return ZX_OK;
  }

  hid::DeviceDescriptor* desc;
  if (hid::ParseReportDescriptor(result->desc.data(), result->desc.count(), &desc) !=
      hid::kParseOk) {
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
  info->client = std::move(client);
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
  status = fdio_service_connect(service, channel_remote.release());
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

  auto& client = *info.client;

  // Get the report event.
  //
  // // Get the report event.
  zx::event report_event;
  {
    auto result = client.GetReportsEvent();
    if ((result.status() != ZX_OK) || (result->status != ZX_OK)) {
      return 1;
    }
    report_event = std::move(result->event);
  }

  // Watch the power button device for reports
  while (true) {
    report_event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr);

    auto result = client.ReadReport();
    if (result.status() != ZX_OK || result->status != ZX_OK) {
      return 1;
    }

    const fidl::VectorView<uint8_t>& report = result->data;

    // Ignore reports from different report IDs
    if (info.has_report_id_byte && report[0] != info.report_id) {
      printf("pwrbtn-monitor: input-watcher: wrong id\n");
      continue;
    }

    // Check if the power button is pressed, and request a poweroff if so.
    const size_t byte_index = info.has_report_id_byte + info.bit_offset / 8;
    if (report[byte_index] & (1u << (info.bit_offset % 8))) {
      auto status = send_poweroff();
      if (status != ZX_OK) {
        printf("pwrbtn-monitor: input-watcher: failed send poweroff to device manager.\n");
        continue;
      }
    }
  }
}
