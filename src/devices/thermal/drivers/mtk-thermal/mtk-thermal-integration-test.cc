// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

zx_status_t GetDeviceHandle(const char* path, zx::channel* handle) {
  fbl::unique_fd fd(open(path, O_RDWR));
  if (fd.get() < -1) {
    fprintf(stderr, "Failed to open device: %d\n", fd.get());
    return ZX_ERR_IO;
  }

  zx_status_t status = fdio_get_service_handle(fd.release(), handle->reset_and_get_address());
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to get FDIO handle for thermal device: %d\n", status);
  }
  return status;
}

bool IsMt8167() {
  constexpr char kSysInfoPath[] = "/svc/fuchsia.sysinfo.SysInfo";
  zx::channel channel;
  zx_status_t status = GetDeviceHandle(kSysInfoPath, &channel);
  if (status != ZX_OK)
    return false;

  char board_name[fuchsia_sysinfo_BOARD_NAME_LEN + 1];
  size_t actual_size;
  zx_status_t fidl_status = fuchsia_sysinfo_SysInfoGetBoardName(channel.get(), &status, board_name,
                                                                sizeof(board_name), &actual_size);
  if (fidl_status != ZX_OK || status != ZX_OK) {
    return false;
  }
  board_name[actual_size] = '\0';

  if (!strcmp(board_name, "mt8167s_ref")) {
    return true;
  }
  if (!strcmp(board_name, "cleo")) {
    return true;
  }
  return false;
}

TEST(Thermal, ConstantVoltage) {
  if (!IsMt8167()) {
    fprintf(stderr, "Skipping because not Mt8167 board\n");
    return;
  }
  zx::channel channel;
  zx_status_t status = GetDeviceHandle("/dev/class/thermal/000", &channel);
  ASSERT_OK(status);
  fuchsia_hardware_thermal_ThermalDeviceInfo info;
  zx_status_t status2;
  status = fuchsia_hardware_thermal_DeviceGetDeviceInfo(channel.get(), &status2, &info);
  ASSERT_OK(status);
  ASSERT_OK(status2);
  uint16_t opp;
  constexpr uint32_t kDomain = fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN;
  status =
      fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint(channel.get(), kDomain, &status2, &opp);
  ASSERT_OK(status);
  ASSERT_OK(status2);

  // Any OPP that's in use should be the same voltage.
  constexpr uint32_t kConstantVoltage = 1'300'000;
  EXPECT_EQ(kConstantVoltage, info.opps[kDomain].opp[opp].volt_uv);
}
