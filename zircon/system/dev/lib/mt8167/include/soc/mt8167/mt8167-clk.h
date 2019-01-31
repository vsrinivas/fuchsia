// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace board_mt8167 {

enum Mt8167Clk {
    kClkThermal,
    kClkI2c0,
    kClkI2c1,
    kClkI2c2,
    kClkPmicWrapAp,
    kClkPmicWrap26M,
    kClkAuxAdc,
    kClkSlowMfg,
    kClkAxiMfg,
    kClkMfgMm,
    kClkAud1,
    kClkAud2,
    kClkAudEngen1,
    kClkAudEngen2,
};

}  // namespace board_mt8167
