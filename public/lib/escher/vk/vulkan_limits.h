// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

namespace escher {

static constexpr uint64_t kNumAttachments = 8;
static constexpr uint64_t kNumBindings = 16;
static constexpr uint64_t kNumDescriptorSets = 4;
static constexpr uint64_t kNumVertexAttributes = 16;
static constexpr uint64_t kNumVertexBuffers = 4;
static constexpr uint64_t kPushConstantSize = 128;

}  // namespace escher
