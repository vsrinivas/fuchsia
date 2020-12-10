// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_DRAWABLE_FLAGS_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_DRAWABLE_FLAGS_H_

#include <cstdint>

#include "src/ui/lib/escher/util/enum_flags.h"

namespace escher {

// These flags modify how |PaperDrawables| are rendered by |PaperRenderer|.
// They become part of the |PaperDrawCalls| generated from the |PaperDrawable|
// by |PaperDrawCallFactory|.
enum class PaperDrawableFlagBits : uint8_t {
  kDebug = 1U << 0,
  kDisableShadowCasting = 1U << 1,
  // Apply a BT.709 OETF (essentially a "gamma correction" curve) to the texture sampled in the
  // fragment shader.  More precisely, a close approximation to the OETF, achieved by squaring
  // the RGB components of the sampled texture.
  kBt709Oetf = 1U << 2,

  kAllFlags = 0x7u,
};
ESCHER_DECLARE_ENUM_FLAGS(PaperDrawableFlags, PaperDrawableFlagBits);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_DRAWABLE_FLAGS_H_
