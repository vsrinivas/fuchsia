// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bad-block.h"

#include "aml-bad-block.h"

namespace nand {

zx_status_t BadBlock::Create(Config config, fbl::RefPtr<BadBlock>* out) {
    switch (config.bad_block_config.type) {
    case kAmlogicUboot:
        return AmlBadBlock::Create(config, out);
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

} // namespace nand
