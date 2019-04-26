// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_PAPER_PAPER_DRAW_CALL_FACTORY_H_
#define LIB_ESCHER_PAPER_PAPER_DRAW_CALL_FACTORY_H_

#include "lib/escher/paper/paper_readme.h"

#include "lib/escher/geometry/types.h"
#include "lib/escher/paper/paper_drawable_flags.h"
#include "lib/escher/renderer/uniform_binding.h"
#include "lib/escher/util/hash_map.h"

namespace escher {

struct RoundedRectSpec;

// |PaperDrawCallFactory| is responsible for generating |PaperDrawCalls| and
// enqueuing them on a |PaperRenderQueue|.  It is hidden from clients of
// |PaperRenderer|, except for those who implement their own subclasses of
// |PaperDrawable|.
class PaperDrawCallFactory final {
 public:
  // |weak_escher| is used only to create |white_texture_|; it is not retained.
  PaperDrawCallFactory(EscherWeakPtr weak_escher,
                       const PaperRendererConfig& config);
  PaperDrawCallFactory(const PaperDrawCallFactory&) = delete;
  ~PaperDrawCallFactory();

  // Draw the specified shape by obtaining a mesh from |PaperShapeCache| and
  // generating/enqueuing draw calls via |EnqueueDrawCalls()|.
  void DrawCircle(float radius, const PaperMaterial& material,
                  PaperDrawableFlags flags = {});
  void DrawRect(vec2 min, vec2 max, const PaperMaterial& material,
                PaperDrawableFlags flags = {});
  void DrawRoundedRect(const RoundedRectSpec& spec,
                       const PaperMaterial& material,
                       PaperDrawableFlags flags = {});

  // Generate and enqueue 0 or more draw calls for the mesh/material combo.
  // The mesh is transformed into world space by the matrix atop the transform
  // stack.
  // NOTE: this should probably be private, but it is currently exposed
  // to allow PaperLegacyDrawable to draw arbitrary meshes (these can't cast
  // shadows, unfortunately).
  void EnqueueDrawCalls(const PaperShapeCacheEntry& cache_entry,
                        const PaperMaterial& material,
                        PaperDrawableFlags flags);

  // Helper for the creation of uint64_t sort-keys for the opaque and
  // translucent RenderQueues.
  class SortKey {
   public:
    static SortKey NewOpaque(Hash pipeline_hash, Hash draw_hash, float depth);
    static SortKey NewTranslucent(Hash pipeline_hash, Hash draw_hash,
                                  float depth);
    SortKey(const SortKey& other) : key_(other.key_) {}

    uint64_t key() const { return key_; }

   private:
    SortKey(uint64_t key) : key_(key) {}
    uint64_t key_;
  };

 private:
  friend class PaperRenderer;
  friend class PaperTester;

  // Called by |PaperRenderer::SetConfig()|.
  // TODO(ES-151): Currently a no-op.  In order to support other rendering
  // techniques, |PaperDrawCallFactory| will need to be in charge of managing
  // shader variations.
  void SetConfig(const PaperRendererConfig& config);

  // Called by |PaperRenderer::BeginFrame()|.  Returns a vector of
  // UniformBindings; PaperRenderer should bind these before directing the
  // PaperRenderQueue to emit commands into a CommandBuffer.
  //
  // |frame| is used to allocate per-frame memory for draw-calls.
  //
  // |scene| and |camera| are used to generate the |UniformBindings| that are
  // returned from this method, which contain camera and lighting parameters
  // that are shared between multiple draw calls.  This data is opaque to
  // |PaperRenderer|; the precise format is specific to the configuration set
  // by |SetConfig()|.
  //
  // |transform_stack| is used to obtain the model-to-world matrix that is part
  // of each draw call, and to provide clip-planes when obtaining cached meshes
  // from |shape_cache|.
  //
  // |camera| and |transform_stack| could be used to obtain LOD-appropriate
  // meshes from |shape_cache|, but this is not currently implemented.
  void BeginFrame(const FramePtr& frame, PaperScene* scene,
                  PaperTransformStack* transform_stack,
                  PaperRenderQueue* render_queue, PaperShapeCache* shape_cache,
                  vec3 camera_pos, vec3 camera_dir);
  // Cleanup.
  void EndFrame();

  // Rather than using a separate Vulkan pipeline for Materials that have no
  // texture (only a color), we use a 1x1 texture with a single white pixel.
  // This is simpler to implement and avoids the cost of switching pipelines.
  TexturePtr white_texture_;

  FramePtr frame_;
  PaperTransformStack* transform_stack_ = nullptr;
  PaperRenderQueue* render_queue_ = nullptr;
  PaperShapeCache* shape_cache_ = nullptr;
  vec3 camera_pos_;
  vec3 camera_dir_;

  // Cache for |object_data| used by RenderQueueItems in both the opaque and
  // translucent queues.
  HashMap<Hash, void*> object_data_;
};

}  // namespace escher

#endif  // LIB_ESCHER_PAPER_PAPER_DRAW_CALL_FACTORY_H_
