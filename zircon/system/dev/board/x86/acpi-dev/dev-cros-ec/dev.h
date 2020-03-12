// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BOARD_X86_ACPI_DEV_DEV_CROS_EC_DEV_H_
#define ZIRCON_SYSTEM_DEV_BOARD_X86_ACPI_DEV_DEV_CROS_EC_DEV_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <memory>

#include <acpica/acpi.h>
#include <chromiumos-platform-ec/ec_commands.h>
#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>
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
  virtual zx_status_t IssueCommand(uint16_t command, uint8_t command_version, const void* out,
                                   size_t outsize, void* in, size_t insize, size_t* actual);

  // Return true if the platform supports the given feature.
  virtual bool SupportsFeature(enum ec_feature_code feature) const = 0;
};

// Initialise detected devices in the DDK. Exposed for testing.
zx_status_t InitDevices(fbl::RefPtr<EmbeddedController> controller, zx_device_t* parent,
                        ACPI_HANDLE acpi_handle);

}  // namespace cros_ec

#endif  // ZIRCON_SYSTEM_DEV_BOARD_X86_ACPI_DEV_DEV_CROS_EC_DEV_H_
