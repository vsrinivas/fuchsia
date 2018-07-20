// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_H_
#define LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/geometry/clip_planes.h"
#include "lib/escher/renderer/render_queue.h"
#include "lib/escher/renderer/uniform_allocation.h"
#include "lib/escher/util/hash_map.h"

namespace escher {

// Supports rendering of escher::Model and escher::Objects.  Encapsulates two
// RenderQueues, one for opaque and one for translucent objects.
class PaperRenderQueue {
 public:
  explicit PaperRenderQueue(EscherWeakPtr escher);

  // Set up view-point specific data that is used for the rest of the frame.
  // This includes both uniforms that will be passed to shaders, as well as
  // camera position and orientation that will be used for generating
  // RenderQueueItem sort-keys.
  void InitFrame(const FramePtr& frame, const Stage& stage,
                 const Camera& camera);

  // Generate an appropriate RenderQueueItem for the object, and push it onto
  // the appropriate RenderQueue (either opaque or translucent).
  void PushObject(const Object& obj);

  // Sort the opaque/translucent RenderQueues.
  void Sort();

  // Set the CommandBuffer state for opaque rendering and invoke
  // GenerateCommands() on the opaque RenderQueue.  Then, set the
  // CommandBuffer state for translucent rendering and invoke GenerateCommands()
  // on the translucent RenderQueue.
  void GenerateCommands(CommandBuffer* cmd_buf,
                        const CommandBuffer::SavedState* state) const;

  // Clear per-frame data, as well as the opaque/translucent RenderQueues.
  void Clear();

  // Set clip planes that will be applied to all subsequent calls of PushModel()
  // and PushObject(), until the next time InitFrame() is called.
  void SetClipPlanes(const ClipPlanes& planes) { clip_planes_ = planes; }

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
  const EscherWeakPtr escher_;

  ShaderProgramPtr program_;

  // escher::Object does not include meshes for rectangles and circles, instead
  // expecting them to be provided by the renderer.
  MeshPtr rectangle_;
  MeshPtr circle_;

  // Rather than using a separate Vulkan pipeline for Materials that have no
  // texture (only a color), we use a 1x1 texture with a single white pixel.
  // This is simpler to implement and avoids the cost of switching pipelines.
  TexturePtr white_texture_;

  RenderQueue opaque_;
  RenderQueue translucent_;

  ClipPlanes clip_planes_;

  // Per-frame data.
  FramePtr frame_;
  vec3 camera_dir_;
  vec3 camera_pos_;
  UniformAllocation view_projection_uniform_;
  // Cache for |object_data| used by RenderQueueItems in both the opaque and
  // translucent queues.
  HashMap<Hash, void*> object_data_;
};

}  // namespace escher

#endif  // LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_H_
