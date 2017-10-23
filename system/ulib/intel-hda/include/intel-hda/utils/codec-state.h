// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace audio {
namespace intel_hda {

#include <stdint.h>

// Section 7.3.3.12.  Present only in pin complexes
struct PinWidgetCtrlState {
    PinWidgetCtrlState() { }
    explicit PinWidgetCtrlState(uint8_t raw_data) : raw_data_(raw_data) { }

    bool   hp_amp_enb()  const { return (raw_data_ & (1u << 7)) != 0; }
    bool   output_enb()  const { return (raw_data_ & (1u << 6)) != 0; }
    bool   input_enb()   const { return (raw_data_ & (1u << 5)) != 0; }
    VRefEn vref_enb()    const { return static_cast<VRefEn>(raw_data_ & 0xf); }
    EPT    ept()         const { return static_cast<EPT>(raw_data_ & 0x3); }

    uint8_t raw_data_;
};

// Section 7.3.3.15 and Table 92
struct PinSenseState {
    PinSenseState() { }
    explicit PinSenseState(uint32_t raw_data) : raw_data_(raw_data) { }

    bool     presence_detect() const { return (raw_data_ & 0x80000000u) != 0; }
    bool     eld_valid()       const { return (raw_data_ & 0x40000000u) != 0; }
    uint32_t impedance()       const { return (raw_data_ & 0x7fffffffu); }

    uint32_t raw_data_ = 0;
};

// Section 7.3.3.16
struct EAPDState {
    EAPDState() { }
    explicit EAPDState(uint32_t raw_data) : raw_data_(raw_data) { }

    bool btl()     const { return (raw_data_ & 0x1u) != 0; }
    bool eapd()    const { return (raw_data_ & 0x2u) != 0; }
    bool lr_swap() const { return (raw_data_ & 0x4u) != 0; }

    uint32_t raw_data_;
};

}  // namespace audio
}  // namespace intel_hda
