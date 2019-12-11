// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <fcntl.h>
#include <fuchsia/hardware/ftdi/llcpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <filesystem>
#include <iostream>

void PrintHelp() {
  printf(
      "Usage: create_ssd1306 \n \
      create_ssd1306: This program creates an I2C bus on the FTDI 232H breakout chip \n \
         and programs it to have the ssd1306 display brought up as an I2C device. If this \n \
         completes successfully, `dm dump` should have the 'ftdi-i2c' device and the \n \
         'ssd1306' device. The ssd1306 device should appear under /dev/class/dotmatrix-display \n \
\n \
         PLEASE NOTE: The I2C bus on the 232H must be used as follows: \n \
            Pin 0 - SCL \n \
            Pins 1 & 2 - SDA and must be wired together\n");
}

int main(int argc, char** argv) {
  if (argc > 1) {
    PrintHelp();
    return 0;
  }

  const char* path = "/dev/class/serial-impl/";
  int fd = -1;
  for (const auto& entry : std::filesystem::directory_iterator(path)) {
    fd = open(entry.path().c_str(), O_RDWR);
    if (fd > 0) {
      break;
    }
  }
  if (fd <= 0) {
    printf("Open serial-impl failed with %d\n", fd);
    return 1;
  }

  zx_handle_t handle;
  zx_status_t status = fdio_get_service_handle(fd, &handle);
  if (status != ZX_OK) {
    printf("Create FIDL handle failed with %d\n", status);
    return 1;
  }

  // This wires the 0 pin as SCL and pins 1 & 2 as SDA.
  ::llcpp::fuchsia::hardware::ftdi::I2cBusLayout layout = {0, 1, 2};
  ::llcpp::fuchsia::hardware::ftdi::I2cDevice i2c_dev = {
      // This is the I2C address for the SSD1306.
      0x3c,
      // These are the SSD1306 driver binding rules.
      PDEV_VID_GENERIC,
      PDEV_PID_GENERIC,
      PDEV_DID_SSD1306};

  auto resp = ::llcpp::fuchsia::hardware::ftdi::Device::Call::CreateI2C(zx::unowned_channel(handle), layout, i2c_dev);
  status = resp.status();
  if (status != ZX_OK) {
    printf("Create I2C device failed with %d\n", status);
    return 1;
  }

  return 0;
}
