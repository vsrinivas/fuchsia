// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi/fidl.h"

namespace acpi {

EvaluateObjectFidlHelper EvaluateObjectFidlHelper::FromRequest(acpi::Acpi* acpi, ACPI_HANDLE device,
                                                               EvaluateObjectRequestView& request) {
  std::string path(request->path.data(), request->path.size());
  return EvaluateObjectFidlHelper(acpi, device, std::move(path));
}

acpi::status<fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResult>
EvaluateObjectFidlHelper::Evaluate() {
  auto path = ValidateAndLookupPath();
  if (path.is_error()) {
    return path.take_error();
  }

  return acpi::error(AE_NOT_IMPLEMENTED);
}

acpi::status<std::string> EvaluateObjectFidlHelper::ValidateAndLookupPath() {
  auto result = acpi_->GetHandle(device_handle_, request_path_.data());
  if (result.is_error()) {
    return result.take_error();
  }

  auto my_path = acpi_->GetPath(device_handle_);
  if (my_path.is_error()) {
    return my_path.take_error();
  }

  ACPI_HANDLE target = result.value();
  auto abs_path = acpi_->GetPath(target);
  if (abs_path.is_error()) {
    return abs_path.take_error();
  }

  if (!strncmp(my_path->data(), abs_path->data(), my_path->size())) {
    return acpi::ok(std::move(abs_path.value()));
  }

  return acpi::error(AE_ACCESS);
}

acpi::status<> EvaluateObjectFidlHelper::DecodeParameters() {
  // TODO(fxbug.dev/79172): implement this.
  return acpi::error(AE_NOT_IMPLEMENTED);
}

acpi::status<fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResult>
EvaluateObjectFidlHelper::EncodeReturnValue(ACPI_OBJECT* value) {
  // TODO(fxbug.dev/79172): implement this.
  return acpi::error(AE_NOT_IMPLEMENTED);
}
}  // namespace acpi
