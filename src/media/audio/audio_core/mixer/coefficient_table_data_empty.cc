// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/coefficient_table.h"

namespace media::audio::mixer {

// This file is linked by builds that have no prebuilt coefficient tables.
const cpp20::span<const PrebuiltSincFilterCoefficientTable> kPrebuiltSincFilterCoefficientTables;

}  // namespace media::audio::mixer
