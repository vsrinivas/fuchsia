// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/public/lib/escher/impl/compute_shader.h"

namespace sketchy_service {

// Provides kernel to tessellate strokes on GPU.
class StrokeTessellator final {
 public:
  StrokeTessellator(escher::Escher* escher);
  void Dispatch(escher::BufferPtr stroke_info_buffer,
                escher::BufferPtr control_points_buffer,
                escher::BufferPtr re_params_buffer,
                escher::BufferPtr division_counts_buffer,
                escher::BufferPtr cumulative_division_counts_buffer,
                escher::BufferPtr division_segment_index_buffer,
                escher::BufferPtr vertex_buffer,
                escher::BufferPtr index_buffer,
                escher::impl::CommandBuffer* command,
                const escher::TimestampProfilerPtr& profiler,
                uint32_t division_count);

 private:
  escher::impl::ComputeShader kernel_;
};

}  // namespace sketchy_service
