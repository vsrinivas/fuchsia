// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/ui/fun/sketchy/fidl/types.fidl-common.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/scenic/client/session.h"
#include "garnet/bin/ui/sketchy/buffer.h"
#include "garnet/bin/ui/sketchy/resources/resource.h"
#include "sketchy/stroke_segment.h"

namespace sketchy_service {

class Stroke;
using StrokePtr = fxl::RefPtr<Stroke>;
using StrokePath = std::vector<sketchy::StrokeSegment>;

class StrokeGroup;

class Stroke final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  Stroke(escher::Escher* escher);
  bool SetPath(sketchy::StrokePathPtr path);

  // Tessellate and merge the mesh into larger buffers of |stroke_group|.
  void TessellateAndMerge(escher::impl::CommandBuffer* command,
                          escher::BufferFactory* buffer_factory,
                          StrokeGroup* stroke_group);

 private:
  escher::Escher* const escher_;
  StrokePath path_;
  float length_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Stroke);
};

}  // namespace sketchy_service
