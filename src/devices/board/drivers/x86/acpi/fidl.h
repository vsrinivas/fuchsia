// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_FIDL_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_FIDL_H_

#include <fuchsia/hardware/acpi/llcpp/fidl.h>

#include <acpica/acpi.h>

#include "src/devices/board/drivers/x86/acpi/acpi.h"

namespace acpi {

class EvaluateObjectFidlHelper {
 public:
  using EvaluateObjectRequestView =
      fidl::WireServer<fuchsia_hardware_acpi::Device>::EvaluateObjectRequestView;
  EvaluateObjectFidlHelper(acpi::Acpi* acpi, ACPI_HANDLE device, std::string request_path)
      : acpi_(acpi), device_handle_(device), request_path_(std::move(request_path)) {}

  static EvaluateObjectFidlHelper FromRequest(acpi::Acpi* acpi, ACPI_HANDLE device,
                                              EvaluateObjectRequestView& request);

  // Calls AcpiEvaluateObject using the arguments supplied to the constructor in |request|, and
  // replies on the completer given in |completer| if the call succeeds.
  // If something fails, acpi::error() is returned with a value that indicates what went wrong.
  acpi::status<fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResult> Evaluate();

  // These methods are public for unit tests only.
  // Validate that the path supplied in the request is a child of the device,
  // and returns the absolute path to the supplied path.
  acpi::status<std::string> ValidateAndLookupPath();

  // Convert our FIDL parameters to ACPI_OBJECTs, and put them in the params_ vector.
  acpi::status<> DecodeParameters();

  // Take the given ACPI_OBJECT and turn it into a DeviceEvaluateObjectResult.
  acpi::status<fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResult> EncodeReturnValue(
      ACPI_OBJECT* value);

 private:
  // State that comes from outside.
  acpi::Acpi* acpi_;
  ACPI_HANDLE device_handle_;
  std::string request_path_;
};

}  // namespace acpi

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_FIDL_H_
