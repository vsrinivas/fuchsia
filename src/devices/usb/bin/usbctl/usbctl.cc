// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/hardware/usb/peripheral/llcpp/fidl.h>
#include <fuchsia/hardware/usb/virtual/bus/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/usb-peripheral-utils/event-watcher.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/fidl.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddk/protocol/usb/modeswitch.h>

#define DEV_USB_PERIPHERAL_DIR "/dev/class/usb-peripheral"

#define MANUFACTURER_STRING "Zircon"
#define CDC_PRODUCT_STRING "CDC Ethernet"
#define UMS_PRODUCT_STRING "USB Mass Storage"
#define TEST_PRODUCT_STRING "USB Function Test"
#define CDC_TEST_PRODUCT_STRING "CDC Ethernet & USB Function Test"
#define SERIAL_STRING "12345678"

namespace peripheral = ::llcpp::fuchsia::hardware::usb::peripheral;

const peripheral::FunctionDescriptor cdc_function_descs[] = {
    {
        .interface_class = USB_CLASS_COMM,
        .interface_subclass = USB_CDC_SUBCLASS_ETHERNET,
        .interface_protocol = 0,
    },
};

const peripheral::FunctionDescriptor ums_function_descs[] = {
    {
        .interface_class = USB_CLASS_MSC,
        .interface_subclass = USB_SUBCLASS_MSC_SCSI,
        .interface_protocol = USB_PROTOCOL_MSC_BULK_ONLY,
    },
};

const peripheral::FunctionDescriptor test_function_descs[] = {
    {
        .interface_class = USB_CLASS_VENDOR,
        .interface_subclass = 0,
        .interface_protocol = 0,
    },
};

const peripheral::FunctionDescriptor cdc_test_function_descs[] = {
    {
        .interface_class = USB_CLASS_COMM,
        .interface_subclass = USB_CDC_SUBCLASS_ETHERNET,
        .interface_protocol = 0,
    },
    {
        .interface_class = USB_CLASS_VENDOR,
        .interface_subclass = 0,
        .interface_protocol = 0,
    },
};

typedef struct {
  const peripheral::FunctionDescriptor* descs;
  size_t descs_count;
  const char* product_string;
  uint16_t vid;
  uint16_t pid;
} usb_config_t;

static const usb_config_t cdc_function = {
    .descs = cdc_function_descs,
    .descs_count = countof(cdc_function_descs),
    .product_string = CDC_PRODUCT_STRING,
    .vid = GOOGLE_USB_VID,
    .pid = GOOGLE_USB_CDC_PID,
};

static const usb_config_t ums_function = {
    .descs = ums_function_descs,
    .descs_count = countof(ums_function_descs),
    .product_string = UMS_PRODUCT_STRING,
    .vid = GOOGLE_USB_VID,
    .pid = GOOGLE_USB_UMS_PID,
};

static const usb_config_t test_function = {
    .descs = test_function_descs,
    .descs_count = countof(test_function_descs),
    .product_string = TEST_PRODUCT_STRING,
    .vid = GOOGLE_USB_VID,
    .pid = GOOGLE_USB_FUNCTION_TEST_PID,
};

static const usb_config_t cdc_test_function = {
    .descs = cdc_test_function_descs,
    .descs_count = countof(cdc_test_function_descs),
    .product_string = CDC_TEST_PRODUCT_STRING,
    .vid = GOOGLE_USB_VID,
    .pid = GOOGLE_USB_CDC_AND_FUNCTION_TEST_PID,
};

static peripheral::DeviceDescriptor device_desc = {
    .bcd_usb = htole16(0x0200),
    .b_device_class = 0,
    .b_device_sub_class = 0,
    .b_device_protocol = 0,
    .b_max_packet_size0 = 64,
    //   idVendor and idProduct are filled in later
    .bcd_device = htole16(0x0100),
    //    iManufacturer, iProduct and iSerialNumber are filled in later
    .b_num_configurations = 1,
};

static int open_usb_device(void) {
  struct dirent* de;
  DIR* dir = opendir(DEV_USB_PERIPHERAL_DIR);
  if (!dir) {
    printf("Error opening %s\n", DEV_USB_PERIPHERAL_DIR);
    return -1;
  }

  while ((de = readdir(dir)) != NULL) {
    char devname[128];

    snprintf(devname, sizeof(devname), "%s/%s", DEV_USB_PERIPHERAL_DIR, de->d_name);
    int fd = open(devname, O_RDWR);
    if (fd < 0) {
      printf("Error opening %s\n", devname);
      continue;
    }

    closedir(dir);
    return fd;
  }

  closedir(dir);
  return -1;
}

static zx_status_t device_init(zx_handle_t svc, const usb_config_t* config) {
  device_desc.id_vendor = htole16(config->vid);
  device_desc.id_product = htole16(config->pid);
  device_desc.manufacturer = fidl::StringView(MANUFACTURER_STRING);
  device_desc.product = fidl::StringView(config->product_string, strlen(config->product_string));
  device_desc.serial = fidl::StringView(SERIAL_STRING);

  peripheral::FunctionDescriptor func_descs[config->descs_count];
  memcpy(func_descs, config->descs, sizeof(peripheral::FunctionDescriptor) * config->descs_count);
  fidl::VectorView<peripheral::FunctionDescriptor> function_descs(func_descs, config->descs_count);

  auto resp = peripheral::Device::Call::SetConfiguration(zx::unowned_channel(svc), device_desc,
                                                         function_descs);
  if (resp.status() != ZX_OK) {
    return resp.status();
  }
  if (resp->result.is_err()) {
    return resp->result.err();
  }
  return ZX_OK;
}

static zx_status_t device_clear_functions(zx_handle_t svc) {
  zx::channel handles[2];
  zx_status_t status = zx::channel::create(0, handles, handles + 1);
  if (status != ZX_OK) {
    return status;
  }
  auto set_result = peripheral::Device::Call::SetStateChangeListener(zx::unowned_channel(svc),
                                                                     std::move(handles[1]));
  if (set_result.status() != ZX_OK) {
    return set_result.status();
  }

  auto clear_functions = peripheral::Device::Call::ClearFunctions(zx::unowned_channel(svc));
  if (clear_functions.status() != ZX_OK) {
    return clear_functions.status();
  }

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  usb_peripheral_utils::EventWatcher watcher(&loop, std::move(handles[0]), 0);
  loop.Run();
  if (!watcher.all_functions_cleared()) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

static int ums_command(zx_handle_t svc, int argc, const char* argv[]) {
  zx_status_t status = device_clear_functions(svc);
  if (status == ZX_OK) {
    status = device_init(svc, &ums_function);
  }

  return status == ZX_OK ? 0 : -1;
}

static int cdc_command(zx_handle_t svc, int argc, const char* argv[]) {
  zx_status_t status = device_clear_functions(svc);
  if (status == ZX_OK) {
    status = device_init(svc, &cdc_function);
  }

  return status == ZX_OK ? 0 : -1;
}

static int test_command(zx_handle_t svc, int argc, const char* argv[]) {
  zx_status_t status = device_clear_functions(svc);
  if (status == ZX_OK) {
    status = device_init(svc, &test_function);
  }

  return status == ZX_OK ? 0 : -1;
}

static int cdc_test_command(zx_handle_t svc, int argc, const char* argv[]) {
  zx_status_t status = device_clear_functions(svc);
  if (status == ZX_OK) {
    status = device_init(svc, &cdc_test_function);
  }

  return status == ZX_OK ? 0 : -1;
}

typedef struct {
  const char* name;
  int (*command)(zx_handle_t svc, int argc, const char* argv[]);
  const char* description;
} usbctl_command_t;

static usbctl_command_t commands[] = {
    {"init-ums", ums_command, "init-ums - initializes the USB Mass Storage function"},
    {"init-cdc", cdc_command, "init-cdc - initializes the CDC Ethernet function"},
    {"init-test", test_command, "init-test - initializes the USB Peripheral Test function"},
    {"init-cdc-test", cdc_test_command,
     "init-cdc-test - initializes CDC plus Test Function composite device"},
    {NULL, NULL, NULL},
};

static void usage(void) {
  fprintf(stderr, "usage: \"usbctl <command>\", where command is one of:\n");

  usbctl_command_t* command = commands;
  while (command->name) {
    fprintf(stderr, "    %s\n", command->description);
    command++;
  }
}

int main(int argc, const char** argv) {
  if (argc < 2) {
    usage();
    return -1;
  }

  int fd = open_usb_device();
  if (fd < 0) {
    fprintf(stderr, "could not find a device in %s\n", DEV_USB_PERIPHERAL_DIR);
    return fd;
  }

  zx_handle_t svc;
  zx_status_t status = fdio_get_service_handle(fd, &svc);
  if (status != ZX_OK) {
    close(fd);
    return status;
  }

  const char* command_name = argv[1];
  usbctl_command_t* command = commands;
  while (command->name) {
    if (!strcmp(command_name, command->name)) {
      status = command->command(svc, argc - 1, argv + 1);
      goto done;
    }
    command++;
  }
  // if we fall through, print usage
  usage();
  status = ZX_ERR_INVALID_ARGS;

done:
  zx_handle_close(svc);
  close(fd);
  return status;
}
