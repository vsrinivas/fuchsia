// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BOARD_X86_ACPI_DEV_DEV_CROS_EC_DEV_H_
#define ZIRCON_SYSTEM_DEV_BOARD_X86_ACPI_DEV_DEV_CROS_EC_DEV_H_

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

class AcpiCrOsEc : public fbl::RefCounted<AcpiCrOsEc> {
 public:
  // Create a new AcpiCrOsEc object.
  static zx_status_t Create(fbl::RefPtr<AcpiCrOsEc>* out);
  ~AcpiCrOsEc();

  // Issue a command to the EC.
  zx_status_t IssueCommand(uint16_t command, uint8_t command_version, const void* out,
                           size_t outsize, void* in, size_t insize, size_t* actual);

  // Return true if the platform has any MotionSense sensors or a FIFO.
  bool supports_motion_sense() const;
  bool supports_motion_sense_fifo() const;

 private:
  AcpiCrOsEc();
  DISALLOW_COPY_ASSIGN_AND_MOVE(AcpiCrOsEc);

  fbl::Mutex io_lock_;
  struct ec_response_get_features features_;
};

}  // namespace cros_ec

#endif  // ZIRCON_SYSTEM_DEV_BOARD_X86_ACPI_DEV_DEV_CROS_EC_DEV_H_
