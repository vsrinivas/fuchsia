// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// virtio-scsi device ABI
// Reference: https://ozlabs.org/~rusty/virtio-spec/virtio-0.9.5.pdf,
//   Appendix I

#pragma once

#include <assert.h>
#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

struct virtio_scsi_config {
  // Number of request (SCSI Command) queues
  uint32_t num_queues;
  uint32_t seg_max;
  uint32_t max_sectors;
  uint32_t cmd_per_lun;
  uint32_t event_info_size;
  uint32_t sense_size;
  uint32_t cdb_size;
  uint16_t max_channel;
  uint16_t max_target;
  uint32_t max_lun;
} __PACKED;

static_assert(sizeof(struct virtio_scsi_config) == 36, "virtio_scsi_config should be 36 bytes.");

#define VIRTIO_SCSI_CDB_DEFAULT_SIZE 32
#define VIRTIO_SCSI_SENSE_DEFAULT_SIZE 96

// A virtio-scsi request represents a single SCSI command to a single target.
// The command command has a 'virtio_scsi_req_cmd' from the driver to the
// device, an optional data out region (again from the driver to the device),
// a virtio_scsi_resp_cmd from the device to the driver with Sense information
// (if any), and an optional data in region.
//
// The virtio_scsi_req_cmd and resp_cmd structures must be in a single virtio
// element unless the F_ANY_LAYOUT feature is negotiated.
struct virtio_scsi_req_cmd {
  uint8_t lun[8];
  // tag must be unique for all commands issued to a LUN
  uint64_t id;
  // SIMPLE, ORDERED, HEAD OF QUEUE, or ACA; virtio-scsi only supports SIMPLE
  uint8_t task_attr;
  uint8_t prio;
  uint8_t crn;
  uint8_t cdb[VIRTIO_SCSI_CDB_DEFAULT_SIZE];
} __PACKED;

static_assert(sizeof(virtio_scsi_req_cmd) == 51, "virtio_scsi_req_cmd should be 51 bytes");

struct virtio_scsi_resp_cmd {
  uint32_t sense_len;
  uint32_t residual;
  uint16_t status_qualifier;
  uint8_t status;
  // Transport-level command response, not SCSI command status.
  // See: ScsiResponse
  uint8_t response;
  uint8_t sense[VIRTIO_SCSI_SENSE_DEFAULT_SIZE];
} __PACKED;

static_assert(sizeof(virtio_scsi_resp_cmd) == 108, "virtio_scsi_resp_cmd should be 108 bytes");

enum class ScsiResponse : uint8_t {
  VIRTIO_SCSI_S_OK = 0,
};

__END_CDECLS
