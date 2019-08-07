// Copyright 2019  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/temperature_fetcher_impl.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include <cmath>

#include <fbl/unique_fd.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <lib/fdio/fdio.h>
#include <trace/event.h>
#include <zircon/status.h>

#include "lib/syslog/cpp/logger.h"

namespace cobalt {

TemperatureFetcherImpl::TemperatureFetcherImpl() { GetDeviceHandle(); }

TemperatureFetchStatus TemperatureFetcherImpl::FetchTemperature(int32_t *temperature) {
  float temperature_float;
  zx_status_t status, status2;
  status = fuchsia_hardware_thermal_DeviceGetTemperatureCelsius(channel_.get(), &status2,
                                                                &temperature_float);
  if (status == ZX_ERR_NOT_SUPPORTED || status2 == ZX_ERR_NOT_SUPPORTED ||
      status == ZX_ERR_BAD_HANDLE || status2 == ZX_ERR_BAD_HANDLE) {
    FX_LOGS(ERROR) << "Cobalt SystemMetricsDaemon: Temperature fetching not supported: "
                   << zx_status_get_string(status) << " " << zx_status_get_string(status2);
    return TemperatureFetchStatus::NOT_SUPPORTED;
  } else if (status != ZX_OK || status2 != ZX_OK) {
    FX_LOGS(ERROR) << "Cobalt SystemMetricsDaemon: Failed to get current temperature: "
                   << zx_status_get_string(status) << " " << zx_status_get_string(status2);
    return TemperatureFetchStatus::FAIL;
  }
  // Round to the nearest degree.
  *temperature = static_cast<int32_t>(std::round(temperature_float));
  TRACE_COUNTER("system_metrics", "temperature", 0, "temperature", *temperature);
  return TemperatureFetchStatus::SUCCEED;
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
