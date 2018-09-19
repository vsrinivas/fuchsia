// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/paper/paper_shape_cache.h"

#include "lib/escher/escher.h"
#include "lib/escher/geometry/bounding_box.h"
#include "lib/escher/geometry/indexed_triangle_mesh_clip.h"
#include "lib/escher/geometry/indexed_triangle_mesh_upload.h"
#include "lib/escher/renderer/batch_gpu_uploader.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/shape/mesh_spec.h"
#include "lib/escher/shape/rounded_rect.h"
#include "lib/escher/util/alloca.h"
#include "lib/escher/util/hasher.h"
#include "lib/escher/util/trace_macros.h"

namespace escher {

PaperShapeCache::PaperShapeCache(EscherWeakPtr escher)
    : escher_(std::move(escher)) {}

PaperShapeCache::~PaperShapeCache() { FXL_DCHECK(!uploader_); }

void PaperShapeCache::BeginFrame(BatchGpuUploader* uploader,
                                 uint64_t frame_number) {
  FXL_DCHECK(uploader && !uploader_ && frame_number >= frame_number_);
  uploader_ = uploader;
  frame_number_ = frame_number;
}

void PaperShapeCache::EndFrame() {
  FXL_DCHECK(uploader_);
  uploader_ = nullptr;

  TRACE_DURATION("gfx", "escher::PaperShapeCache::EndFrame", "cache_hits",
                 cache_hit_count_ + cache_hit_after_plane_culling_count_,
                 "cache_hits_after_plane_culling",
                 cache_hit_after_plane_culling_count_, "cache_misses",
                 cache_miss_count_);
  cache_hit_count_ = 0;
  cache_hit_after_plane_culling_count_ = 0;
  cache_miss_count_ = 0;

  TrimCache();
}

Mesh* PaperShapeCache::GetRoundedRectMesh(const RoundedRectSpec& spec,
                                          const plane2* clip_planes,
                                          size_t num_clip_planes) {
  TRACE_DURATION("gfx", "escher::PaperShapeCache::GetRoundedRectMesh");
  if (spec.width <= 0.f || spec.height <= 0.f)
    return nullptr;

  Hash rect_hash;
  {
    Hasher h;
    h.struc(spec);
    rect_hash = h.value();
  }

  const BoundingBox bounding_box(-0.5f * vec3(spec.width, spec.height, 0),
                                 0.5f * vec3(spec.width, spec.height, 0));

  return GetShapeMesh(
      rect_hash, bounding_box, clip_planes, num_clip_planes,
      [this, &spec, &bounding_box](const plane2* unculled_clip_planes,
                                   size_t num_unculled_clip_planes) {
        // No mesh was found, so we need to generate one.  The steps are:
        //   1) Tessellate the mesh.
        //   2) Clip it against the culled clip planes.  If completely clipped,
        //   add a null cache entry and return.  Otherwise:
        //   3) Post-process the mesh, e.g. to add additional triangles and
        //   vertex attributes to support stencil shadow volumes.
        //   4) Upload the mesh to the GPU, and return it to be cached.

        // Step 1): Tessellate the mesh.

        auto vertex_and_index_counts =
            GetRoundedRectMeshVertexAndIndexCounts(spec);

        MeshSpec mesh_spec{{MeshAttribute::kPosition2D, MeshAttribute::kUV}};

        std::vector<MeshSpec::IndexType> indices(
            vertex_and_index_counts.second);
        std::vector<vec2> positions(vertex_and_index_counts.first);
        std::vector<vec2> attributes(vertex_and_index_counts.first);

        GenerateRoundedRectIndices(
            spec, mesh_spec, indices.data(),
            indices.size() * sizeof(MeshSpec::IndexType));
        GenerateRoundedRectVertices(
            spec, mesh_spec, positions.data(), positions.size() * sizeof(vec2),
            attributes.data(), attributes.size() * sizeof(vec2));

        // Step 2): If necessary clip mesh against remaining clip-planes.

        auto clip_result = IndexedTriangleMeshClip(
            IndexedTriangleMesh2d<vec2>{.indices = std::move(indices),
                                        .positions = std::move(positions),
                                        .attributes1 = std::move(attributes)},
            unculled_clip_planes, num_unculled_clip_planes);

        // Step 3): Post-process the mesh... next CL.

        // Step 4): Upload the mesh to the GPU and return it.

        return IndexedTriangleMeshUpload(escher_.get(), uploader_, mesh_spec,
                                         bounding_box, clip_result.first);
      });
}

// TODO(ES-141): Generalize this to support 3D clip planes.
Mesh* PaperShapeCache::GetShapeMesh(const Hash& shape_hash,
                                    const BoundingBox& bounding_box,
                                    const plane2* clip_planes,
                                    size_t num_clip_planes,
                                    CacheMissMeshGenerator mesh_generator) {
  TRACE_DURATION("gfx", "escher::PaperShapeCache::GetShapeMesh");
  FXL_DCHECK(clip_planes || num_clip_planes == 0);

  // We will first look up the mesh via |lookup_hash|, but may later use
  // |shape_hash| as an optimization.
  Hash lookup_hash;
  {
    Hasher h(shape_hash);
    for (size_t i = 0; i < num_clip_planes; ++i) {
      h.struc(clip_planes[i]);
    }
    lookup_hash = h.value();
  }

  // Attempt to find a pre-clipped shape in the cache.
  // TODO(ES-142): do we need to quantize the clip_planes to avoid
  // numerical precision errors when the planes are transformed into the
  // object's coordinate system?  Seems like this should perhaps be the
  // responsibility of the caller.
  // TODO(ES-142): similarly, the caller should be responsible for
  // sorting the planes if desired.  For example, if the same planes are
  // provided in a different order, the cache would fail to find the pre-clipped
  // mesh.
  if (auto* entry = FindEntry(lookup_hash)) {
    ++cache_hit_count_;
    return entry->mesh.get();
  }

  // There are two separate optimizations to perform against the bounding box:
  //   1) If a plane clips all 8 corners then don't bother considering the other
  //      planes.  Return nullptr because there is nothing to render.
  //   2) If a plane does not clip any of the 8 corners, then proceed to the
  //      next plane.  Don't bother clipping individual triangles with such
  //      planes, because we know they will all pass.
  size_t num_unculled_clip_planes = num_clip_planes;
  plane2* unculled_clip_planes = ESCHER_ALLOCA(plane2, num_clip_planes);
  if (auto bbox_was_completely_clipped = CullPlanesAgainstBoundingBox(
          bounding_box, clip_planes, unculled_clip_planes,
          &num_unculled_clip_planes)) {
    // Cache a null MeshPtr, so that a subsequent lookup won't have to do the
    // CPU work of testing planes against the bounding box.
    ++cache_hit_count_;
    AddEntry(lookup_hash, MeshPtr());
    return nullptr;
  }

  // If some of the planes were culled, recompute the lookup hash and try again.
  Hash lookup_hash2;
  if (num_clip_planes == num_unculled_clip_planes) {
    // No planes were culled; we will need to tessellate/clip/upload a new Mesh.
    // When we cache the new mesh, we will only create one entry for it.
    lookup_hash2 = lookup_hash;
  } else {
    FXL_DCHECK(num_unculled_clip_planes < num_clip_planes) << "insane.";

    // The following is an optimization for the common case where at least one
    // clip plane is culled.  This avoids each frame failing the |lookup_hash|
    // lookup, then culling the clip planes in order to perform a successful
    // lookup with |lookup_hash2|.  Instead, by caching the result with both
    // |lookup_hash| and |lookup_hash2| we allow subsequent frames to succeed
    // immediately with |lookup_hash|.
    //
    // The reason that we don't only store the result under |lookup_hash| is the
    // also-common case where the initial set of planes differs each frame, but
    // the set of unculled planes remaining after culling is the same from frame
    // to frame.  In other words, the case where |lookup_hash| differs, but
    // |lookup_hash2| is the same.  For example, this would occur when the mesh
    // is moving within the interior of a large clip volume.  In this case, all
    // planes are being translated with respect to the mesh, so |lookup_hash|
    // differs, but all planes are being culled, so |lookup_hash2| is the same.

    // Compute |lookup_hash2| like |lookup_hash|, except use culled clip planes.
    Hasher h(shape_hash);
    for (size_t i = 0; i < num_unculled_clip_planes; ++i) {
      h.struc(unculled_clip_planes[i]);
    }
    lookup_hash2 = h.value();

    if (auto* entry = FindEntry(lookup_hash2)) {
      // NOTE: FXL_DCHECK(entry->mesh) might seem reasonable here, but there
      // still is a possibility that the generated mesh will be completely
      // clipped.

      // Woo-hoo!  We found the mesh under the second lookup key.  Before
      // returning it, re-cache it under the original lookup key so that it can
      // be looked up more efficiently next time.
      ++cache_hit_after_plane_culling_count_;
      AddEntry(lookup_hash, entry->mesh);

      // TODO(ES-142): by caching under |lookup_hash| here, there is the
      // possibility of pathological behavior under "stop and go" motion.  For
      // example, if an unclipped mesh stops for a few frames then it will be
      // looked up successfully via |lookup_hash|, but when it starts to move
      // again it may find that the |lookup_hash2| entry has already been
      // evicted.  A solution might a variant AddEntry(key, key2, mesh) such
      // that whenever a mesh is looked up via |key|, the timestamp for |key2|
      // is also updated.

      return entry->mesh.get();
    }
  }
  ++cache_miss_count_;

  MeshPtr mesh;
  {
    TRACE_DURATION("gfx",
                   "escher::PaperShapeCache::GetShapeMesh[mesh_generator]");
    mesh = mesh_generator(unculled_clip_planes, num_unculled_clip_planes);
  }

  AddEntry(lookup_hash, mesh);
  if (lookup_hash2 != lookup_hash) {
    AddEntry(lookup_hash2, mesh);
  }
  return mesh.get();
}

bool PaperShapeCache::CullPlanesAgainstBoundingBox(
    const BoundingBox& bounding_box, const plane2* planes,
    plane2* unculled_planes_out, size_t* num_planes_inout) {
  TRACE_DURATION("gfx",
                 "escher::PaperShapeCache::CullPlanesAgainstBoundingBox");
  const size_t num_planes = *num_planes_inout;
  size_t& num_unculled_planes_out = *num_planes_inout = 0;

  for (size_t i = 0; i < num_planes; ++i) {
    auto& plane = planes[i];
    const uint32_t num_clipped_corners = bounding_box.NumClippedCorners(plane);
    if (num_clipped_corners > 0) {
      unculled_planes_out[num_unculled_planes_out++] = plane;
      if (num_clipped_corners == 8)
        return true;
    }
  }
  return false;
}

PaperShapeCache::CacheEntry* PaperShapeCache::FindEntry(const Hash& hash) {
  auto it = cache_.find(hash);
  if (it != cache_.end()) {
    it->second.last_touched_frame = frame_number_;
    return &it->second;
  }
  return nullptr;
}

void PaperShapeCache::AddEntry(const Hash& hash, MeshPtr mesh) {
  auto it = cache_.find(hash);
  if (it != cache_.end()) {
    FXL_DCHECK(false) << "CacheEntry already exists.";
    return;
  }
  cache_.insert({hash, CacheEntry{.last_touched_frame = frame_number_,
                                  .mesh = std::move(mesh)}});
}

// TODO(SCN-957): rather than rolling our own ad-hoc cache eviction strategy,
// (which is already a performance bottleneck) we should plug in a reusable
// cache that performs better and is well-tested.
void PaperShapeCache::TrimCache() {
  TRACE_DURATION("gfx", "escher::PaperShapeCache::TrimCache", "num_entries",
                 cache_.size());
  for (auto it = cache_.begin(); it != cache_.end();) {
    if (frame_number_ - it->second.last_touched_frame >=
        kNumFramesBeforeEviction) {
      TRACE_DURATION("gfx", "escher::PaperShapeCache::TrimCache[erase]");
      it = cache_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace escher
