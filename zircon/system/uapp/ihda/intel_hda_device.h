// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "zircon_device.h"

namespace audio {
namespace intel_hda {

class IntelHDADevice : public ZirconDevice {
public:
    zx_status_t Probe();

    uint16_t vid()       const { return vid_; }
    uint16_t did()       const { return did_; }
    uint8_t  ihda_vmaj() const { return ihda_vmaj_; }
    uint8_t  ihda_vmin() const { return ihda_vmin_; }
    uint8_t  rev_id()    const { return rev_id_; }
    uint8_t  step_id()   const { return step_id_; }

protected:
    explicit IntelHDADevice(const char* const dev_name) : ZirconDevice(dev_name) { }

    uint16_t vid_       = 0u;
    uint16_t did_       = 0u;
    uint8_t  ihda_vmaj_ = 0u;
    uint8_t  ihda_vmin_ = 0u;
    uint8_t  rev_id_    = 0u;
    uint8_t  step_id_   = 0u;
};

}  // namespace audio
}  // namespace intel_hda
