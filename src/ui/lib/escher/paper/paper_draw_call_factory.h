// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_DRAW_CALL_FACTORY_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_DRAW_CALL_FACTORY_H_

#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/paper/paper_drawable_flags.h"
#include "src/ui/lib/escher/paper/paper_readme.h"
#include "src/ui/lib/escher/renderer/uniform_binding.h"
#include "src/ui/lib/escher/util/hash_map.h"

namespace escher {

struct RoundedRectSpec;

// |PaperDrawCallFactory| is responsible for generating |PaperDrawCalls| and
// enqueuing them on a |PaperRenderQueue|.  It is hidden from clients of
// |PaperRenderer|, except for those who implement their own subclasses of
// |PaperDrawable|.
class PaperDrawCallFactory final {
 public:
  // |weak_escher| is used only to create |white_texture_|; it is not retained.
  PaperDrawCallFactory(EscherWeakPtr weak_escher, const PaperRendererConfig& config);
  PaperDrawCallFactory(const PaperDrawCallFactory&) = delete;
  ~PaperDrawCallFactory();

  // Draw the specified shape by obtaining a mesh from |PaperShapeCache| and
  // generating/enqueuing draw calls via |EnqueueDrawCalls()|.
  void DrawCircle(float radius, const PaperMaterial& material, PaperDrawableFlags flags = {});
  void DrawRect(vec2 min, vec2 max, const PaperMaterial& material, PaperDrawableFlags flags = {});
  void DrawRoundedRect(const RoundedRectSpec& spec, const PaperMaterial& material,
                       PaperDrawableFlags flags = {});
  void DrawBoundingBox(const PaperMaterial& material, PaperDrawableFlags flags = {});

  // We are currently unable to clip meshes that are already provided to us and not generated
  // from the PaperShapeCache, and so we render them directly without doing any clipping. It is
  // possible to clip on the GPU, but this functionality is not available on all hardware, and
  // performing clipping on the GPU means our existing stencil shadow implementation will no
  // longer work.
  void DrawMesh(const MeshPtr& mesh, const PaperMaterial& material, PaperDrawableFlags flags = {});

  // TODO(ES203) - We will eventualy not need to do this as we will simply
  // inject PaperRenderer with a version of the PaperDrawCallFactory that
  // is used explicitly for testing.
  //
  // When this is set to true, no draw calls get enqueued and instead,
  // PaperDrawCallFactory will accumulate a list of cache entries that
  // would have been drawn.
  void set_track_cache_entries(bool track) { track_cache_entries_ = track; }
  const std::vector<PaperShapeCacheEntry>& tracked_cache_entries() const {
    return tracked_cache_entries_;
  }

  // Helper for the creation of uint64_t sort-keys for the opaque and
  // translucent RenderQueues.
  class SortKey {
   public:
    static SortKey NewOpaque(Hash pipeline_hash, Hash draw_hash, float depth);
    static SortKey NewTranslucent(Hash pipeline_hash, Hash draw_hash, float depth);
    static SortKey NewWireframe(Hash pipeline_hash, Hash draw_hash, float depth);
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
  // TODO(fxbug.dev/7242): Currently a no-op.  In order to support other rendering
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
  void BeginFrame(const FramePtr& frame, BatchGpuUploader* uploader, PaperScene* scene,
                  PaperTransformStack* transform_stack, PaperRenderQueue* render_queue,
                  PaperShapeCache* shape_cache, vec3 camera_pos, vec3 camera_dir);
  // Cleanup.
  void EndFrame();

  // Generate and enqueue 0 or more draw calls for the mesh/material combo.
  // The mesh is transformed into world space by the matrix atop the transform
  // stack.
  void EnqueueDrawCalls(const PaperShapeCacheEntry& cache_entry, const PaperMaterial& material,
                        PaperDrawableFlags flags);

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

  bool track_cache_entries_ = false;
  std::vector<PaperShapeCacheEntry> tracked_cache_entries_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_DRAW_CALL_FACTORY_H_
