// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel_hda_device.h"

#include <lib/fdio/io.h>
#include <zircon/device/intel-hda.h>

#include "zircon_device.h"

namespace audio {
namespace intel_hda {

zx_status_t ProbeIntelHdaDevice(ZirconDevice* device, IntelHDADevice* result) {
  zx_status_t res = device->Connect();
  if (res != ZX_OK) {
    return res;
  }

  ihda_get_ids_req_t req;
  ihda_get_ids_resp_t resp;

  ZirconDevice::InitRequest(&req, IHDA_CMD_GET_IDS);
  res = device->CallDevice(req, &resp);
  if (res != ZX_OK) {
    return res;
  }

  result->vid = resp.vid;
  result->did = resp.did;
  result->ihda_vmaj = resp.ihda_vmaj;
  result->ihda_vmin = resp.ihda_vmin;
  result->rev_id = resp.rev_id;
  result->step_id = resp.step_id;

  return ZX_OK;
}

}  // namespace intel_hda
}  // namespace audio
