// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.hardware.input/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/fit/defer.h>
#include <lib/svc/outgoing.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <unordered_map>
#include <utility>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>

#include "src/bringup/bin/pwrbtn-monitor/monitor.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

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
  fidl::WireSyncClient<fuchsia_hardware_input::Device> client;
  uint8_t report_id;
  size_t bit_offset;
  bool has_report_id_byte;
};

static zx_status_t InputDeviceAdded(int dirfd, int event, const char* name, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }

  // Open the fd and get a FIDL client.
  // Note: the rust vfs used in an integration test for this does not support
  // using the DESCRIBE flag with a service node, which all of the functions
  // that use file descriptors send as they are trying to emulate posix
  // semantics. To work around this, clone the fd into a new handle and then use
  // fdio_service_connect_at, which does not use the DESCRIBE flag.
  zx::channel dir_chan;
  zx_status_t status = fdio_fd_clone(dirfd, dir_chan.reset_and_get_address());
  if (status != ZX_OK) {
    printf("pwrbtn-monitor: clone failed: %s\n", zx_status_get_string(status));
    return ZX_OK;
  }

  zx::channel chan, remote_chan;
  status = zx::channel::create(0, &chan, &remote_chan);
  if (status != ZX_OK) {
    printf("pwrbtn-monitor: failed to create channel: %d\n", status);
    return status;
  }
  status = fdio_service_connect_at(dir_chan.get(), name, remote_chan.release());
  if (status != ZX_OK) {
    printf("pwrbtn-monitor: service handle conversion failed: %d\n", status);
    return status;
  }
  auto client = fidl::WireSyncClient<fuchsia_hardware_input::Device>(std::move(chan));

  // Get the report descriptor.
  auto result = client->GetReportDesc();
  if (result.status() != ZX_OK) {
    return ZX_OK;
  }

  hid::DeviceDescriptor* desc;
  if (hid::ParseReportDescriptor(result->desc.data(), result->desc.count(), &desc) !=
      hid::kParseOk) {
    return ZX_OK;
  }
  auto cleanup_desc = fit::defer([desc]() { hid::FreeDeviceDescriptor(desc); });

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

// Open the input directory, wait for the proper input device type to appear,
// and parse out information about the input event itself.
// Args:
//   - event_out: should point to a zx::event that will be populated
//   - info_out: should point to PowerButtonInfo object which will be populated
// Errors:
//   - ZX_ERR_INTERNAL: if the input directory can not be opened
//   - other: errors returned by `fdio_watch_directory`, other than
//            `ZX_ERR_STOP`
zx_status_t get_button_report_event(zx::event* event_out, PowerButtonInfo* info_out) {
  fbl::unique_fd dirfd;
  {
    int fd = open(INPUT_PATH, O_DIRECTORY);
    if (fd < 0) {
      printf("pwrbtn-monitor: Failed to open " INPUT_PATH ": %d\n", errno);
      // TODO(jmatt) is this the right failure code?
      return ZX_ERR_INTERNAL;
    }
    dirfd.reset(fd);
  }

  zx_status_t status =
      fdio_watch_directory(dirfd.get(), InputDeviceAdded, ZX_TIME_INFINITE, info_out);
  if (status != ZX_ERR_STOP) {
    printf("pwrbtn-monitor: Failed to find power button device\n");
    return status;
  }
  dirfd.reset();

  auto& client = info_out->client;

  // Get the report event.
  auto result = client->GetReportsEvent();
  if (result.status() != ZX_OK) {
    printf("pwrbtn-monitor: failed to get report event: %d\n", result.status());
    return result.status();
  }
  if (result->status != ZX_OK) {
    printf("pwrbtn-monitor: failed to get report event: %d\n", result->status);
    return result->status;
  }
  *event_out = std::move(result->event);
  return ZX_OK;
}

// Processes a power button event, dispatches events appropriately to
// listeners, and quits the execution look if reading a report fails
void process_power_event(
    zx::event* report_event, pwrbtn::PowerButtonMonitor* monitor,
    std::unordered_map<size_t, fidl::ServerBindingRef<fuchsia_power_button::Monitor>>* bindings,
    PowerButtonInfo* info, zx_status_t status, async::Loop* loop) {
  if (status == ZX_ERR_CANCELED) {
    return;
  }
  auto result = info->client->ReadReport();
  if (result.status() != ZX_OK) {
    printf("pwrbtn-monitor: failed to read report: %d\n", result.status());
    loop->Quit();
    return;
  }
  if (result->status != ZX_OK) {
    printf("pwrbtn-monitor: failed to read report: %d\n", result->status);
    loop->Quit();
    return;
  }

  // Ignore reports from different report IDs
  const fidl::VectorView<uint8_t>& report = result->data;
  if (info->has_report_id_byte && report[0] != info->report_id) {
    printf("pwrbtn-monitor: input-watcher: wrong id\n");
    return;
  }

  // Check if the power button is pressed, and request a poweroff if so.
  const size_t byte_index = info->has_report_id_byte + info->bit_offset / 8;
  if (report[byte_index] & (1u << (info->bit_offset % 8))) {
    // Sends a Press event to clients, regardless of the Action set.
    for (auto& binding : *bindings) {
      monitor->SendButtonEvent(binding.second,
                               fuchsia_power_button::wire::PowerButtonEvent::kPress);
    }

    auto status = monitor->DoAction();
    if (status != ZX_OK) {
      printf("pwrbtn-monitor: input-watcher: failed to handle press.\n");
      return;
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  zx_status_t status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    return 1;
  }

  // Declare the looper
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  // Declare some structures needed for the duration of the program, some of
  // these are shared between different tasks.
  std::unordered_map<size_t, fidl::ServerBindingRef<fuchsia_power_button::Monitor>> bindings;
  pwrbtn::PowerButtonMonitor monitor;
  PowerButtonInfo info;

  // Create a task which watches for the power button device to appear and then
  // starts monitoring it for events.
  zx::event report_event;
  async::Wait pwrbtn_waiter(ZX_HANDLE_INVALID, ZX_USER_SIGNAL_0, 0,
                            [&pwrbtn_waiter, &loop, &monitor, &bindings, &info, &report_event](
                                async_dispatcher_t*, async::Wait*, zx_status_t status,
                                const zx_packet_signal_t*) mutable {
                              process_power_event(&report_event, &monitor, &bindings, &info, status,
                                                  &loop);
                              pwrbtn_waiter.Begin(loop.dispatcher());
                            });

  async::TaskClosure button_init([&report_event, &info, &pwrbtn_waiter, &loop]() mutable {
    zx_status_t status = get_button_report_event(&report_event, &info);

    if (status != ZX_OK) {
      printf("pwrbtn-monitor: failure getting button report event, exiting\n");
      exit(1);
    }

    pwrbtn_waiter.set_object(report_event.get());

    // schedule the watcher task
    pwrbtn_waiter.Begin(loop.dispatcher());
  });

  button_init.Post(loop.dispatcher());

  svc::Outgoing outgoing(loop.dispatcher());
  status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    printf("pwrbtn-monitor: failed to ServeFromStartupInfo: %s\n", zx_status_get_string(status));
    return 1;
  }

  async_dispatcher_t* dispatcher = loop.dispatcher();
  size_t n_bindings = 0;
  status = outgoing.svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<fuchsia_power_button::Monitor>,
      fbl::MakeRefCounted<fs::Service>(
          [&monitor, dispatcher, &bindings,
           &n_bindings](fidl::ServerEnd<fuchsia_power_button::Monitor> request) mutable {
            fidl::OnUnboundFn<pwrbtn::PowerButtonMonitor> unbound_handler =
                [&bindings, n_bindings](pwrbtn::PowerButtonMonitor* /*unused*/,
                                        fidl::UnbindInfo info,
                                        fidl::ServerEnd<fuchsia_power_button::Monitor> /*unused*/) {
                  if (info.is_peer_closed()) {
                    bindings.erase(n_bindings);
                  }
                };

            auto binding = fidl::BindServer(dispatcher, std::move(request), &monitor,
                                            std::move(unbound_handler));
            bindings.emplace(n_bindings, std::move(binding));
            ++n_bindings;

            return ZX_OK;
          }));
  if (status != ZX_OK) {
    printf("pwrbtn-monitor: failed to AddEntry: %s\n", zx_status_get_string(status));
    return 1;
  }

  loop.Run();
  return 1;
}
