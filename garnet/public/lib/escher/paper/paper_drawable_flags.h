// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_PAPER_PAPER_DRAWABLE_FLAGS_H_
#define LIB_ESCHER_PAPER_PAPER_DRAWABLE_FLAGS_H_

#include "lib/escher/util/enum_flags.h"

namespace escher {

// These flags modify how |PaperDrawables| are rendered by |PaperRenderer|.
// They become part of the |PaperDrawCalls| generated from the |PaperDrawable|
// by |PaperDrawCallFactory|.
enum class PaperDrawableFlagBits : uint8_t {
  kDebug = 1U << 0,
  kDisableShadowCasting = 1U << 1,

  kAllFlags = 0x3u,
};
ESCHER_DECLARE_ENUM_FLAGS(PaperDrawableFlags, PaperDrawableFlagBits);

}  // namespace escher

#endif  // LIB_ESCHER_PAPER_PAPER_DRAWABLE_FLAGS_H_
