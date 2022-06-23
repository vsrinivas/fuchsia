// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/span.h>

#include "src/media/audio/lib/processing/coefficient_table.h"

namespace media_audio {

// This file is linked by builds that have no prebuilt coefficient tables.
const cpp20::span<const PrebuiltSincFilterCoefficientTable> kPrebuiltSincFilterCoefficientTables;

}  // namespace media_audio
