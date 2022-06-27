// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/lib/acpi/power-resource.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <lib/ddk/debug.h>

namespace acpi {

zx_status_t PowerResource::Init() {
  auto power_resource = acpi_->EvaluateObject(acpi_handle_, nullptr, std::nullopt);
  if (power_resource.is_error()) {
    zxlogf(ERROR, "Failed to evaluate ACPI PowerResource object: %d", power_resource.error_value());
    return power_resource.zx_status_value();
  }

  system_level_ = static_cast<uint8_t>(power_resource->PowerResource.SystemLevel);
  resource_order_ = static_cast<uint16_t>(power_resource->PowerResource.ResourceOrder);

  auto power_resource_status = acpi_->EvaluateObject(acpi_handle_, "_STA", std::nullopt);
  if (power_resource_status.is_ok()) {
    is_on_ = power_resource_status->Integer.Value == 1;
  }

  return ZX_OK;
}

zx_status_t PowerResource::Reference() {
  if (ref_count_ == 0) {
    auto status = acpi_->EvaluateObject(acpi_handle_, "_ON", std::nullopt);
    if (status.is_error()) {
      zxlogf(ERROR, "Failed to call _ON on an ACPI power resource: %s",
             zx_status_get_string(status.zx_status_value()));
      return status.zx_status_value();
    }
    is_on_ = true;
  }

  ++ref_count_;

  return ZX_OK;
}

zx_status_t PowerResource::Dereference() {
  ZX_ASSERT_MSG(ref_count_ > 0, "Tried to dereference an ACPI PowerResource with no references");

  if (ref_count_ == 1) {
    auto status = acpi_->EvaluateObject(acpi_handle_, "_OFF", std::nullopt);
    if (status.is_error()) {
      zxlogf(ERROR, "Failed to call _OFF on an ACPI power resource: %s",
             zx_status_get_string(status.zx_status_value()));
      return status.zx_status_value();
    }
    is_on_ = false;
  }

  --ref_count_;

  return ZX_OK;
}

}  // namespace acpi
