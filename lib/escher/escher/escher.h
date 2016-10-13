// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "escher/forward_declarations.h"
#include "escher/status.h"
#include "ftl/macros.h"

namespace escher {

// Escher is the primary class used by clients of the Escher library.
class Escher {
 public:
  // Escher does not take ownership of the objects in the Vulkan context.  It is
  // up to the application to eventually destroy them, and also to ensure that
  // they outlive the Escher instance.
  Escher(const VulkanContext& context, const VulkanSwapchain& swapchain);
  ~Escher();

  MeshBuilderPtr NewMeshBuilder(const MeshSpec& spec,
                                size_t max_vertex_count,
                                size_t max_index_count);

  PaperRendererPtr NewPaperRenderer();

 private:
  std::unique_ptr<impl::EscherImpl> impl_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Escher);
};

}  // namespace escher
