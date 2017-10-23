// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/intel-hda.h>
#include <fdio/io.h>

#include "intel_hda_device.h"

namespace audio {
namespace intel_hda {

zx_status_t IntelHDADevice::Probe() {
    zx_status_t res = ZirconDevice::Connect();
    if (res != ZX_OK)
        return res;

    ihda_get_ids_req_t req;
    ihda_get_ids_resp_t resp;

    InitRequest(&req, IHDA_CMD_GET_IDS);
    res = CallDevice(req, &resp);
    if (res != ZX_OK)
        return res;

    vid_       = resp.vid;
    did_       = resp.did;
    ihda_vmaj_ = resp.ihda_vmaj;
    ihda_vmin_ = resp.ihda_vmin;
    rev_id_    = resp.rev_id;
    step_id_   = resp.step_id;

    return ZX_OK;
}

}  // namespace audio
}  // namespace intel_hda
