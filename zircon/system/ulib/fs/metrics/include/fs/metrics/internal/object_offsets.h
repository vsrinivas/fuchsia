// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fs/metrics/internal/object_generator.h>
#include <fs/metrics/internal/offsets.h>

namespace fs_metrics {
// This library provides a helper struct that specializes both ObjectGenerator and Offsets with the
// same argument pack. This provides a single point of update. For documentation see ObjectGenerator
// and Offsets.

template <typename... AttributeList>
struct ObjectOffsets : Offsets<AttributeList...>, ObjectGenerator<AttributeList...> {};

} // namespace fs_metrics
