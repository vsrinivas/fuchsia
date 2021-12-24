// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/hardware/power/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <algorithm>

#include <fbl/algorithm.h>
#include <fbl/string_buffer.h>
#include <fbl/vector.h>

typedef struct {
  int type;
  char name[255];
  uint8_t state;
  zx_handle_t events;
  zx_handle_t fidl_channel;
} pwrdev_t;

struct arg_data {
  bool debug;
  bool poll_events;
};

static const char* type_to_string[] = {"AC", "battery"};

zx_status_t get_source_info(zx_handle_t channel, struct fuchsia_hardware_power_SourceInfo* info) {
  zx_status_t status, op_status;

  // If either fails return the error we see (0 is success, errors are negative)
  status = fuchsia_hardware_power_SourceGetPowerInfo(channel, &op_status, info);
  zx_status_t result = std::min(status, op_status);
  if (result != ZX_OK) {
    fprintf(stderr, "SourceGetPowerInfo failed (transport: %d, operation: %d)\n", status,
            op_status);
  }
  return result;
}

static const char* state_to_string[] = {"online", "discharging", "charging", "critical"};
static const char* state_offline = "offline/not present";
const char* get_state_string(uint32_t state, fbl::StringBuffer<256>* buf) {
  buf->Clear();
  for (size_t i = 0; i < std::size(state_to_string); i++) {
    if (state & (1 << i)) {
      if (buf->length()) {
        buf->Append(", ");
      }
      buf->Append(state_to_string[i]);
    }
  }

  return (buf->length() > 0) ? buf->c_str() : state_offline;
}

static zx_status_t get_battery_info(zx_handle_t ch) {
  struct fuchsia_hardware_power_BatteryInfo binfo = {};
  zx_status_t op_status;
  zx_status_t status = fuchsia_hardware_power_SourceGetBatteryInfo(ch, &op_status, &binfo);
  if (status != ZX_OK) {
    printf("GetBatteryInfo returned %d\n", status);
    return status;
  }

  const char* unit = (binfo.unit == fuchsia_hardware_power_BatteryUnit_MW) ? "mW" : "mA";
  printf("             design capacity: %d %s\n", binfo.design_capacity, unit);
  printf("          last full capacity: %d %s\n", binfo.last_full_capacity, unit);
  printf("              design voltage: %d mV\n", binfo.design_voltage);
  printf("            warning capacity: %d %s\n", binfo.capacity_warning, unit);
  printf("                low capacity: %d %s\n", binfo.capacity_low, unit);
  printf("     low/warning granularity: %d %s\n", binfo.capacity_granularity_low_warning, unit);
  printf("    warning/full granularity: %d %s\n", binfo.capacity_granularity_warning_full, unit);
  printf("                present rate: %d %s\n", binfo.present_rate, unit);
  printf("          remaining capacity: %d %s\n", binfo.remaining_capacity, unit);
  printf("             present voltage: %d mV\n", binfo.present_voltage);
  printf("==========================================\n");
  printf("remaining battery percentage: %d %%\n",
         binfo.remaining_capacity * 100 / binfo.last_full_capacity);
  if (binfo.present_rate < 0) {
    printf("      remaining battery life: %.2f h\n",
           (float)binfo.remaining_capacity / (float)binfo.present_rate * -1);
  }
  putchar('\n');
  return ZX_OK;
}

void parse_arguments(int argc, char** argv, struct arg_data* args) {
  int opt;
  while ((opt = getopt(argc, argv, "p")) != -1) {
    switch (opt) {
      case 'p':
        args->poll_events = true;
        break;
      default:
        fprintf(stderr, "Invalid arg: %c\nUsage: %s [-p]\n", opt, argv[0]);
        exit(EXIT_FAILURE);
    }
  }
}

void handle_event(pwrdev_t& interface) {
  zx_status_t status;
  struct fuchsia_hardware_power_SourceInfo info;
  if ((status = get_source_info(interface.fidl_channel, &info)) != ZX_OK) {
    exit(EXIT_FAILURE);
  }

  fbl::StringBuffer<256> old_buf;
  fbl::StringBuffer<256> new_buf;
  printf("%s (%s): state change %s (%#x) -> %s (%#x)\n", interface.name,
         type_to_string[interface.type], get_state_string(interface.state, &old_buf),
         interface.state, get_state_string(info.state, &new_buf), info.state);

  if (interface.type == fuchsia_hardware_power_PowerType_BATTERY &&
      (info.state & fuchsia_hardware_power_POWER_STATE_ONLINE)) {
    if (get_battery_info(interface.fidl_channel) != ZX_OK) {
      exit(EXIT_FAILURE);
    }
  }

  interface.state = info.state;
}

void poll_events(const fbl::Vector<pwrdev_t>& interfaces) {
  zx_wait_item_t* items = new zx_wait_item_t[interfaces.size()];
  for (size_t i = 0; i < interfaces.size(); i++) {
    items[i].handle = interfaces[i].events;
    items[i].waitfor = ZX_USER_SIGNAL_0;
    items[i].pending = 0;
  }

  zx_status_t status;
  printf("waiting for events...\n\n");
  for (;;) {
    status = zx_object_wait_many(items, interfaces.size(), ZX_TIME_INFINITE);
    if (status != ZX_OK) {
      printf("zx_object_wait_many() returned %d\n", status);
      exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < interfaces.size(); i++) {
      if (items[i].pending & ZX_USER_SIGNAL_0) {
        handle_event(interfaces[i]);
      }
    }
  }
}

int main(int argc, char** argv) {
  struct arg_data args = {};
  parse_arguments(argc, argv, &args);

  struct dirent* de;
  DIR* dir = opendir("/dev/class/power");
  if (!dir) {
    printf("Failed to read /dev/class/power\n");
    exit(EXIT_FAILURE);
  }

  fbl::StringBuffer<256> state_str;
  fbl::Vector<pwrdev_t> interfaces;
  while ((de = readdir(dir)) != NULL) {
    int fd = openat(dirfd(dir), de->d_name, O_RDONLY);
    if (fd < 0) {
      printf("Failed to read %s, skipping: %d\n", de->d_name, fd);
      continue;
    }

    struct fuchsia_hardware_power_SourceInfo pinfo;
    zx_handle_t ch;
    zx_status_t status;
    zx_status_t op_status;

    status = fdio_get_service_handle(fd, &ch);
    if (status != ZX_OK) {
      printf("Failed to get service handle for %s, skipping: %d!\n", de->d_name, status);
      continue;
    }

    status = get_source_info(ch, &pinfo);
    if (status != ZX_OK) {
      printf("Failed to read from source %s, skipping\n", de->d_name);
      continue;
    }

    printf("[%s] type: %s, state: %s (%#x)\n", de->d_name, type_to_string[pinfo.type],
           get_state_string(pinfo.state, &state_str), pinfo.state);

    if (pinfo.type == fuchsia_hardware_power_PowerType_BATTERY &&
        (pinfo.state & fuchsia_hardware_power_POWER_STATE_ONLINE)) {
      if (get_battery_info(ch) != ZX_OK) {
        fprintf(stderr, "Couldn't read battery information for %s, skipping\n", de->d_name);
        continue;
      }
    }

    if (args.poll_events) {
      zx_handle_t h = ZX_HANDLE_INVALID;
      status = fuchsia_hardware_power_SourceGetStateChangeEvent(ch, &op_status, &h);
      if (status != ZX_OK || op_status != ZX_OK) {
        printf("failed to get event: %d / %d\n", status, op_status);
        return status;
      }

      pwrdev_t dev = {};
      dev.type = pinfo.type;
      dev.state = pinfo.state;
      dev.fidl_channel = ch;
      dev.events = h;
      memcpy(dev.name, de->d_name, sizeof(dev.name));
      interfaces.push_back(dev);
    }
  }

  if (args.poll_events) {
    poll_events(interfaces);
  }

  return 0;
}
