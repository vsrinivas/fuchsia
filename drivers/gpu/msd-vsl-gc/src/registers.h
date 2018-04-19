// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_H
#define REGISTERS_H

#include "magma_util/macros.h"
#include "magma_util/register_bitfields.h"
#include "magma_util/register_io.h"

namespace registers {

class ChipId : public magma::RegisterBase {
public:
    DEF_FIELD(31, 0, chip_id);

    static auto Get() { return magma::RegisterAddr<ChipId>(0x20); }
};

} // namespace registers

#endif // REGISTERS_H
