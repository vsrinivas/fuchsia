// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_LIB_SCSI_INCLUDE_LIB_SCSI_SCSILIB_CONTROLLER_H_
#define SRC_DEVICES_BLOCK_LIB_SCSI_INCLUDE_LIB_SCSI_SCSILIB_CONTROLLER_H_

#include <stdint.h>
#include <sys/uio.h>
#include <zircon/types.h>

namespace scsi {

class Controller {
 public:
  // Synchronously execute a SCSI command on the device at target:lun.
  // |cdb| contains the SCSI CDB to execute.
  // |data_out| and |data_in| are optional data-out and data-in regions.
  // Returns ZX_OK if the command was successful at both the transport layer and no check
  // condition happened.
  virtual zx_status_t ExecuteCommandSync(uint8_t target, uint16_t lun, struct iovec cdb,
                                         struct iovec data_out, struct iovec data_in) = 0;
  virtual zx_status_t ExecuteCommandAsync(uint8_t target, uint16_t lun, struct iovec cdb,
                                          struct iovec data_out, struct iovec data_in,
                                          void (*cb)(void*, zx_status_t), void* cookie) = 0;
};

}  // namespace scsi

#endif  // SRC_DEVICES_BLOCK_LIB_SCSI_INCLUDE_LIB_SCSI_SCSILIB_CONTROLLER_H_
