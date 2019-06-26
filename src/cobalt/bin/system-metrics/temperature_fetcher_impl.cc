// Copyright 2019  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/temperature_fetcher_impl.h"

#include <errno.h>
#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <trace/event.h>
#include <zircon/status.h>

#include "lib/syslog/cpp/logger.h"

namespace cobalt {

TemperatureFetcherImpl::TemperatureFetcherImpl() { GetDeviceHandle(); }

bool TemperatureFetcherImpl::FetchTemperature(uint32_t *temperature) {
  zx_status_t status, status2;
  status = fuchsia_hardware_thermal_DeviceGetTemperature(channel_.get(), &status2, temperature);
  if (status != ZX_OK || status2 != ZX_OK) {
    FX_LOGS(ERROR) << "Cobalt SystemMetricsDaemon: Failed to get current "
                   << "temperature: " << zx_status_get_string(status) << " "
                   << zx_status_get_string(status2);
    return false;
  }
  TRACE_COUNTER("system_metrics", "temperature", 0, "temperature", *temperature);
  return true;
}

zx_status_t TemperatureFetcherImpl::GetDeviceHandle() {
  fbl::unique_fd fd(open("/dev/class/thermal/000", O_RDWR));
  if (fd.get() < 0) {
    FX_LOGS(ERROR) << "Failed to open thermal device: " << fd.get() << ", errno=" << errno;
    return ZX_ERR_IO;
  }
  zx_status_t status = fdio_get_service_handle(fd.release(), channel_.reset_and_get_address());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get FDIO handle for thermal device: " << status;
  }
  return status;
}

}  // namespace cobalt
