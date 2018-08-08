// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_ID_H
#define DEVICE_ID_H

#include <stdint.h>

class DeviceId {
public:
    static bool is_gen9(uint32_t device_id)
    {
        switch (device_id) {
            case 0x1912: // Intel(R) HD Graphics 530 (Skylake GT2)
            case 0x1916: // Intel(R) HD Graphics 520 (Skylake GT2)
            case 0x191E: // Intel(R) HD Graphics 515 (Skylake GT2)
            case 0x1926: // Intel(R) Iris Graphics 540 (Skylake GT3e)
            case 0x193b: // Intel(R) Iris Pro Graphics 580 (Skylake GT4e)
            case 0x5916: // Intel(R) HD Graphics 620 (Kabylake GT2)
            case 0x591E: // Intel(R) HD Graphics 615 (Kabylake GT2)
            case 0x5926: // Intel(R) Iris Graphics 640 (Kabylake GT3e)
            case 0x5927: // Intel(R) Iris Graphics 650 (Kabylake GT3e)
                return true;
        }
        return false;
    }
};

#endif // DEVICE_ID_H
