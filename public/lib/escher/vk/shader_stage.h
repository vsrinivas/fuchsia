// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace escher {

enum class ShaderStage {
  kVertex = 0,
  kTessellationControl = 1,
  kTessellationEvaluation = 2,
  kGeometry = 3,
  kFragment = 4,
  kCompute = 5,
  kEnumCount
};

}  // namespace escher
