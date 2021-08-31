// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_CROS_EC_DEV_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_CROS_EC_DEV_H_

#include <fuchsia/hardware/hidbus/cpp/banjo.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <memory>

#include <acpica/acpi.h>
#include <chromiumos-platform-ec/ec_commands.h>
#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/span.h>
#include <fbl/vector.h>

namespace cros_ec {

class EmbeddedController : public fbl::RefCounted<EmbeddedController> {
 public:
  virtual ~EmbeddedController() {}

  // Issue a command to the EC.
  virtual zx_status_t IssueCommand(uint16_t command, uint8_t command_version, const void* input,
                                   size_t input_size, void* result, size_t result_buff_size,
                                   size_t* actual) = 0;

  // Return true if the platform supports the given feature.
  virtual bool SupportsFeature(enum ec_feature_code feature) const = 0;

  // Send a fixed-sized command to the EC with a fixed-size output.
  //
  // These call into the raw "IssueCommand" method above.
  template <typename Input, typename Output>
  zx_status_t IssueCommand(uint16_t command, uint8_t command_version, const Input& input,
                           Output& output);
  template <typename Output>
  zx_status_t IssueCommand(uint16_t command, uint8_t command_version, Output& output);
};

// Initialise detected devices in the DDK. Exposed for testing.
zx_status_t InitDevices(fbl::RefPtr<EmbeddedController> controller, zx_device_t* parent,
                        ACPI_HANDLE acpi_handle);

//
// Implementation details follow.
//

// Send a fixed-sized command to the EC with a fixed-size output.
template <typename Input, typename Output>
zx_status_t EmbeddedController::IssueCommand(uint16_t command, uint8_t command_version,
                                             const Input& input, Output& output) {
  static_assert(!std::is_pointer<Input>::value, "Input type should be a reference, not pointer.");
  static_assert(!std::is_pointer<Output>::value, "Output type should be a reference, not pointer.");
  static_assert(std::is_pod<Input>::value, "Input type should be POD.");
  static_assert(std::is_pod<Output>::value, "Output type should be POD.");

  size_t actual;
  zx_status_t status = this->IssueCommand(command, command_version, &input, sizeof(input), &output,
                                          sizeof(output), &actual);
  if (status != ZX_OK) {
    return status;
  }
  if (actual != sizeof(output)) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

// Send a command with no input to the EC with a fixed-size output.
template <typename Output>
zx_status_t EmbeddedController::IssueCommand(uint16_t command, uint8_t command_version,
                                             Output& output) {
  static_assert(!std::is_pointer<Output>::value, "Output type should be a reference, not pointer.");
  static_assert(std::is_pod<Output>::value, "Output type should be POD.");

  size_t actual;
  zx_status_t status =
      this->IssueCommand(command, command_version, nullptr, 0, &output, sizeof(output), &actual);
  if (status != ZX_OK) {
    return status;
  }
  if (actual != sizeof(output)) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

}  // namespace cros_ec

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_CROS_EC_DEV_H_
