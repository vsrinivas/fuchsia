// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/paper/paper_shape_cache.h"

#include <unordered_set>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/mesh/indexed_triangle_mesh_clip.h"
#include "src/ui/lib/escher/mesh/indexed_triangle_mesh_upload.h"
#include "src/ui/lib/escher/mesh/tessellation.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/shape/rounded_rect.h"
#include "src/ui/lib/escher/util/alloca.h"
#include "src/ui/lib/escher/util/hasher.h"
#include "src/ui/lib/escher/util/pair_hasher.h"
#include "src/ui/lib/escher/util/trace_macros.h"

namespace escher {

namespace {

// Returned when there is no shape to draw, for example when a circle with zero
// radius is requested, or when all vertices of a tessellated shape are clipped
// by clip planes.
const PaperShapeCacheEntry kNullCacheEntry;

// Helper used by GetRoundedRectMesh() and others.  Defined as a standalone
// function instead of a private method on PaperShapeCache to avoid needing to
// include any IndexedTriangleMesh headers in our header.
PaperShapeCacheEntry ProcessTriangleMesh2d(IndexedTriangleMesh2d<vec2> mesh,
                                           const MeshSpec& mesh_spec, const plane3* clip_planes3,
                                           size_t num_clip_planes, const BoundingBox& bounding_box,
                                           PaperRendererShadowType shadow_type, Escher* escher,
                                           BatchGpuUploader* uploader) {
  TRACE_DURATION("gfx", "PaperShapeCache::ProcessTriangleMesh2d");
  FX_DCHECK(mesh_spec == PaperShapeCache::kStandardMeshSpec());

  // Convert 3d clip planes to 2d before clipping.
  std::vector<plane2> clip_planes_vec;
  for (size_t i = 0; i < num_clip_planes; ++i) {
    // Check to make sure the incoming 3D clip plane is not parallel
    // to the z=0 plane. If the two planes intersect, add in the
    // constructed plane2 to the vector of plane2s.
    if (1.f - fabs(clip_planes3[i].dir().z) > 0.001) {
      clip_planes_vec.push_back(plane2(clip_planes3[i]));
    };
  }

  // Grab the raw pointer data from the vector
  // and update the total number of clip planes.
  plane2* clip_planes = clip_planes_vec.data();
  num_clip_planes = clip_planes_vec.size();

  IndexedTriangleMesh2d<vec2> tri_mesh;
  std::tie(tri_mesh, std::ignore) =
      IndexedTriangleMeshClip(std::move(mesh), clip_planes, num_clip_planes);

  switch (shadow_type) {
    case PaperRendererShadowType::kShadowVolume: {
      TRACE_DURATION("gfx", "PaperShapeCache::ProcessTriangleMesh2d[shadow_volume]");

      using Edge = IndexedTriangleMesh2d<vec2>::EdgeType;
      std::unordered_set<Edge, PairHasher> silhouette_edges;

      const uint32_t original_index_count = tri_mesh.index_count();
      const uint32_t original_vertex_count = tri_mesh.vertex_count();
      auto& indices = tri_mesh.indices;

      // Find silhouette edges, and generate opposite face of the shadow volume.
      {
        TRACE_DURATION("gfx", "PaperShapeCache::ProcessTriangleMesh2d[shadow_volume_1]");

        // We're going to double the number of indices in order to mirror the
        // opposite face of the shadow volume, and then add 6 indices (two
        // triangles) per silhouette edge to connect the two faces together with
        // quads.  Empirically, we estimate that there is about one silhouette
        // edge per triangle of the original mesh.
        indices.reserve(tri_mesh.index_count() * 2 + tri_mesh.triangle_count() * 6);

        for (size_t i = 0; i < original_index_count; i += 3) {
          MeshSpec::IndexType* tri = indices.data() + i;
          for (size_t j = 0; j < 3; ++j) {
            // Mirror the triangle on the opposite face of the shadow volume.
            // The index order is reversed.
            indices.push_back(tri[(3 - j) % 3] + original_vertex_count);

            // Look for silhouette edges.
            Edge edge{tri[j], tri[(j + 1) % 3]};
            Edge opposite{edge.second, edge.first};

            auto it = silhouette_edges.find(opposite);
            if (it != silhouette_edges.end()) {
              // The opposite edge was already seen in the mesh, so neither this
              // edge nor the opposite can be a silhouette edge.
              silhouette_edges.erase(it);
            } else {
              // No opposite edge was found, so this edge is a candidate to be a
              // silhouette edge (of course, it may be removed later if its
              // opposite appears).
              silhouette_edges.insert(edge);
            }
          }
        }
      }

      // Finish creating the mesh.  Extrude side faces, copy vertex attributes,
      // and add an additional kBlendWeight1 attribute for computing the shape
      // of the volume in the vertex shader.
      IndexedTriangleMesh2d<vec2, float> out_mesh;
      {
        TRACE_DURATION("gfx", "PaperShapeCache::ProcessTriangleMesh2d[shadow_volume_2]");

        // Extrude side faces between matching silhouette edges.  Flip the edge
        // direction in order to maintain the desired winding order.
        for (auto& edge : silhouette_edges) {
          indices.push_back(edge.second);
          indices.push_back(edge.first);
          indices.push_back(edge.first + original_vertex_count);
          indices.push_back(edge.first + original_vertex_count);
          indices.push_back(edge.second + original_vertex_count);
          indices.push_back(edge.second);
        }

        // Create the output mesh.  Gank the modified indices from the previous
        // mesh, then duplicate the mirrored vertices (there will be exactly
        // twice as many vertices in the new mesh).  We also need an additional
        // attribute that is used as a switch, so that the mirrored vertices are
        // "extruded" away from the light source, whereas the original vertices
        // are left in their original world-space positions; this attribute is 0
        // for original vertices and 1 for mirrored vertices.
        out_mesh.indices = std::move(tri_mesh.indices);
        out_mesh.positions.reserve(original_vertex_count * 2);
        out_mesh.attributes1.reserve(original_vertex_count * 2);
        out_mesh.attributes2.reserve(original_vertex_count * 2);
        for (size_t i = 0; i < original_vertex_count; ++i) {
          out_mesh.positions.push_back(tri_mesh.positions[i]);
          out_mesh.attributes1.push_back(tri_mesh.attributes1[i]);
          out_mesh.attributes2.push_back(0);
        }
        for (size_t i = 0; i < original_vertex_count; ++i) {
          out_mesh.positions.push_back(tri_mesh.positions[i]);
          out_mesh.attributes1.push_back(tri_mesh.attributes1[i]);
          out_mesh.attributes2.push_back(1);
        }
      }

      FX_DCHECK(out_mesh.IsValid());
      FX_DCHECK(mesh_spec == PaperShapeCache::kStandardMeshSpec());

      const uint32_t shadow_volume_index_count = out_mesh.index_count();
      return PaperShapeCacheEntry{
          .mesh =
              IndexedTriangleMeshUpload(escher, uploader, PaperShapeCache::kShadowVolumeMeshSpec(),
                                        bounding_box, std::move(out_mesh)),
          .num_indices = original_index_count,
          .num_shadow_volume_indices = shadow_volume_index_count,
      };
    } break;
    default:
      auto num_indices = tri_mesh.index_count();
      return PaperShapeCacheEntry{
          .mesh = IndexedTriangleMeshUpload(escher, uploader, mesh_spec, bounding_box,
                                            std::move(tri_mesh)),
          .num_indices = num_indices,
          .num_shadow_volume_indices = 0,
      };
  }
}

PaperShapeCacheEntry ProcessTriangleMesh3d(IndexedTriangleMesh3d<vec2> mesh,
                                           const MeshSpec& mesh_spec, const plane3* clip_planes,
                                           size_t num_clip_planes, const BoundingBox& bounding_box,
                                           PaperRendererShadowType shadow_type, Escher* escher,
                                           BatchGpuUploader* uploader) {
  TRACE_DURATION("gfx", "PaperShapeCache::ProcessTriangleMesh3d");
  FX_DCHECK((mesh_spec == MeshSpec{{MeshAttribute::kPosition3D, MeshAttribute::kUV}}));

  IndexedTriangleMesh3d<vec2> tri_mesh;
  std::tie(tri_mesh, std::ignore) =
      IndexedTriangleMeshClip(std::move(mesh), clip_planes, num_clip_planes);

  auto index_count = tri_mesh.index_count();
  return PaperShapeCacheEntry{
      .mesh =
          IndexedTriangleMeshUpload(escher, uploader, mesh_spec, bounding_box, std::move(tri_mesh)),
      .num_indices = index_count,
      .num_shadow_volume_indices = 0,
  };
}
}  // namespace

PaperShapeCache::PaperShapeCache(EscherWeakPtr escher, const PaperRendererConfig& config)
    : escher_(std::move(escher)), shadow_type_(config.shadow_type) {}

PaperShapeCache::~PaperShapeCache() { FX_DCHECK(!uploader_); }

void PaperShapeCache::BeginFrame(BatchGpuUploader* uploader, uint64_t frame_number) {
  FX_DCHECK(uploader && !uploader_);
  uploader_ = uploader;

  // Workaround because Scenic Screenshotter always uses frame #0.
  if (frame_number > 0) {
    FX_DCHECK(frame_number >= frame_number_)
        << "old/new frame#: " << frame_number_ << "/" << frame_number;
    frame_number_ = frame_number;
  }
}

void PaperShapeCache::EndFrame() {
  FX_DCHECK(uploader_);
  uploader_ = nullptr;

  TRACE_DURATION("gfx", "PaperShapeCache::EndFrame", "cache_hits",
                 cache_hit_count_ + cache_hit_after_plane_culling_count_,
                 "cache_hits_after_plane_culling", cache_hit_after_plane_culling_count_,
                 "cache_misses", cache_miss_count_);
  cache_hit_count_ = 0;
  cache_hit_after_plane_culling_count_ = 0;
  cache_miss_count_ = 0;

  TrimCache();
}

void PaperShapeCache::SetConfig(const PaperRendererConfig& config) {
  FX_DCHECK(!uploader_) << "Cannot change config in the middle of a frame.";
  if (shadow_type_ == config.shadow_type)
    return;

  shadow_type_ = config.shadow_type;

  // NOTE: could optimize this to retain cached meshes in some cases.  For
  // example, switching shadow types kShadowMap <--> kNone.  For now we just
  // blow away the cache any time there is a change.
  cache_.clear();
}

const PaperShapeCacheEntry& PaperShapeCache::GetRoundedRectMesh(const RoundedRectSpec& spec,
                                                                const plane3* clip_planes,
                                                                size_t num_clip_planes) {
  TRACE_DURATION("gfx", "PaperShapeCache::GetRoundedRectMesh");
  if (spec.width <= 0.f || spec.height <= 0.f)
    return kNullCacheEntry;

  Hash rect_hash;
  {
    Hasher h;
    h.u32(EnumCast(ShapeType::kRoundedRect));
    h.struc(spec);
    rect_hash = h.value();
  }

  const BoundingBox bounding_box(-0.5f * vec3(spec.width, spec.height, 0),
                                 0.5f * vec3(spec.width, spec.height, 0));

  return GetShapeMesh(
      rect_hash, bounding_box, clip_planes, num_clip_planes,
      [this, &spec, &bounding_box](const plane3* unculled_clip_planes,
                                   size_t num_unculled_clip_planes) {
        // No mesh was found, so we need to generate one.

        uint32_t vertex_count;
        uint32_t index_count;
        std::tie(vertex_count, index_count) = GetRoundedRectMeshVertexAndIndexCounts(spec);

        MeshSpec mesh_spec{{MeshAttribute::kPosition2D, MeshAttribute::kUV}};
        IndexedTriangleMesh2d<vec2> mesh;
        mesh.resize_indices(index_count);
        mesh.resize_vertices(vertex_count);

        GenerateRoundedRectIndices(spec, mesh_spec, mesh.indices.data(),
                                   static_cast<uint32_t>(mesh.total_index_bytes()));
        GenerateRoundedRectVertices(spec, mesh_spec, mesh.positions.data(),
                                    static_cast<uint32_t>(mesh.total_position_bytes()),
                                    mesh.attributes1.data(),
                                    static_cast<uint32_t>(mesh.total_attribute1_bytes()));

        return ProcessTriangleMesh2d(std::move(mesh), mesh_spec, unculled_clip_planes,
                                     num_unculled_clip_planes, bounding_box, shadow_type_,
                                     escher_.get(), uploader_);
      });
}

const PaperShapeCacheEntry& PaperShapeCache::GetCircleMesh(float radius, const plane3* clip_planes,
                                                           size_t num_clip_planes) {
  TRACE_DURATION("gfx", "PaperShapeCache::GetCircleMesh");
  if (radius <= 0.f)
    return kNullCacheEntry;

  Hash circle_hash;
  {
    Hasher h;
    h.u32(EnumCast(ShapeType::kCircle));
    h.f32(radius);
    circle_hash = h.value();
  }

  const BoundingBox bounding_box(-vec3(radius, radius, 0), vec3(radius, radius, 0));

  return GetShapeMesh(
      circle_hash, bounding_box, clip_planes, num_clip_planes,
      [this, radius, &bounding_box](const plane3* unculled_clip_planes,
                                    size_t num_unculled_clip_planes) {
        // No mesh was found, so we need to generate one.

        MeshSpec mesh_spec{{MeshAttribute::kPosition2D, MeshAttribute::kUV}};
        constexpr uint32_t kCircleSubdivisions = 3;
        IndexedTriangleMesh2d<vec2> mesh =
            NewCircleIndexedTriangleMesh(mesh_spec, kCircleSubdivisions, vec2(0, 0), radius);

        return ProcessTriangleMesh2d(std::move(mesh), mesh_spec, unculled_clip_planes,
                                     num_unculled_clip_planes, bounding_box, shadow_type_,
                                     escher_.get(), uploader_);
      });
}

const PaperShapeCacheEntry& PaperShapeCache::GetRectMesh(vec2 min, vec2 max,
                                                         const plane3* clip_planes,
                                                         size_t num_clip_planes) {
  TRACE_DURATION("gfx", "PaperShapeCache::GetRectMesh");

  const BoundingBox bounding_box = BoundingBox::NewChecked(vec3(min, 0), vec3(max, 0), 1);
  if (bounding_box.is_empty()) {
    return kNullCacheEntry;
  }

  Hash rect_hash;
  {
    Hasher h;
    h.u32(EnumCast(ShapeType::kRect));
    h.f32(min.x);
    h.f32(min.y);
    h.f32(max.x);
    h.f32(max.y);
    rect_hash = h.value();
  }

  return GetShapeMesh(
      rect_hash, bounding_box, clip_planes, num_clip_planes,
      [this, min, max, &bounding_box](const plane3* unculled_clip_planes,
                                      size_t num_unculled_clip_planes) {
        // No mesh was found, so we need to generate one.

        MeshSpec mesh_spec{{MeshAttribute::kPosition2D, MeshAttribute::kUV}};
        IndexedTriangleMesh2d<vec2> mesh;
        mesh.indices = std::vector<MeshSpec::IndexType>{0, 1, 2, 0, 2, 3};
        mesh.positions = std::vector<vec2>{vec2(min.x, min.y), vec2(max.x, min.y),
                                           vec2(max.x, max.y), vec2(min.x, max.y)};
        mesh.attributes1 = std::vector<vec2>{vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1)};

        return ProcessTriangleMesh2d(std::move(mesh), mesh_spec, unculled_clip_planes,
                                     num_unculled_clip_planes, bounding_box, shadow_type_,
                                     escher_.get(), uploader_);
      });
}

const PaperShapeCacheEntry& PaperShapeCache::GetBoxMesh(const plane3* clip_planes,
                                                        size_t num_clip_planes) {
  Hash box_hash;
  {
    Hasher h;
    h.u32(EnumCast(ShapeType::kBox));
    box_hash = h.value();
  }

  const BoundingBox bounding_box(vec3(0, 0, 0), vec3(1, 1, 1));
  return GetShapeMesh(
      box_hash, bounding_box, nullptr, 0,
      [this, &bounding_box](const plane3* unculled_clip_planes, size_t num_unculled_clip_planes) {
        // No mesh was found, so we need to generate one.
        MeshSpec mesh_spec{{MeshAttribute::kPosition3D, MeshAttribute::kUV}};
        IndexedTriangleMesh3d<vec2> mesh = NewCubeIndexedTriangleMesh(mesh_spec);
        return ProcessTriangleMesh3d(std::move(mesh), mesh_spec, unculled_clip_planes,
                                     num_unculled_clip_planes, bounding_box, shadow_type_,
                                     escher_.get(), uploader_);
      });
}

const PaperShapeCacheEntry& PaperShapeCache::GetShapeMesh(const Hash& shape_hash,
                                                          const BoundingBox& bounding_box,
                                                          const plane3* clip_planes,
                                                          size_t num_clip_planes,
                                                          CacheMissMeshGenerator mesh_generator) {
  TRACE_DURATION("gfx", "PaperShapeCache::GetShapeMesh");
  FX_DCHECK(clip_planes || num_clip_planes == 0);

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
  // TODO(fxbug.dev/7233): do we need to quantize the clip_planes to avoid
  // numerical precision errors when the planes are transformed into the
  // object's coordinate system?  Seems like this should perhaps be the
  // responsibility of the caller.
  // TODO(fxbug.dev/7233): similarly, the caller should be responsible for
  // sorting the planes if desired.  For example, if the same planes are
  // provided in a different order, the cache would fail to find the pre-clipped
  // mesh.
  if (auto* entry = FindEntry(lookup_hash)) {
    ++cache_hit_count_;
    return *entry;
  }

  // There are two separate optimizations to perform against the bounding box:
  //   1) If a plane clips all 8 corners then don't bother considering the other
  //      planes.  Return nullptr because there is nothing to render.
  //   2) If a plane does not clip any of the 8 corners, then proceed to the
  //      next plane.  Don't bother clipping individual triangles with such
  //      planes, because we know they will all pass.
  size_t num_unculled_clip_planes = num_clip_planes;
  plane3* unculled_clip_planes = ESCHER_ALLOCA(plane3, num_clip_planes);
  if (auto bbox_was_completely_clipped = CullPlanesAgainstBoundingBox(
          bounding_box, clip_planes, unculled_clip_planes, &num_unculled_clip_planes)) {
    // Cache a null MeshPtr, so that a subsequent lookup won't have to do
    // the CPU work of testing planes against the bounding box.
    ++cache_hit_count_;

    AddEntry(lookup_hash, kNullCacheEntry);
    return kNullCacheEntry;
  }

  // If some of the planes were culled, recompute the lookup hash and try again.
  Hash lookup_hash2;
  if (num_clip_planes == num_unculled_clip_planes) {
    // No planes were culled; we will need to tessellate/clip/upload a new Mesh.
    // When we cache the new mesh, we will only create one entry for it.
    lookup_hash2 = lookup_hash;
  } else {
    FX_DCHECK(num_unculled_clip_planes < num_clip_planes) << "insane.";

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
      // NOTE: FX_DCHECK(entry->mesh) might seem reasonable here, but there
      // still is a possibility that the generated mesh will be completely
      // clipped.

      // Woo-hoo!  We found the mesh under the second lookup key.  Before
      // returning it, re-cache it under the original lookup key so that it can
      // be looked up more efficiently next time.
      ++cache_hit_after_plane_culling_count_;
      AddEntry(lookup_hash, *entry);

      // TODO(fxbug.dev/7233): by caching under |lookup_hash| here, there is the
      // possibility of pathological behavior under "stop and go" motion.  For
      // example, if an unclipped mesh stops for a few frames then it will be
      // looked up successfully via |lookup_hash|, but when it starts to move
      // again it may find that the |lookup_hash2| entry has already been
      // evicted.  A solution might a variant AddEntry(key, key2, mesh) such
      // that whenever a mesh is looked up via |key|, the timestamp for |key2|
      // is also updated.

      return *entry;
    }
  }
  ++cache_miss_count_;

  PaperShapeCacheEntry new_entry;
  {
    TRACE_DURATION("gfx", "PaperShapeCache::GetShapeMesh[mesh_generator]");
    new_entry = mesh_generator(unculled_clip_planes, num_unculled_clip_planes);
  }

  AddEntry(lookup_hash, new_entry);
  if (lookup_hash2 != lookup_hash) {
    AddEntry(lookup_hash2, new_entry);
  }
  // Slightly inefficient to look up the newly-inserted entry, but nothing
  // compared to what we just did to create/upload the new mesh.
  return *FindEntry(lookup_hash);
}

bool PaperShapeCache::CullPlanesAgainstBoundingBox(const BoundingBox& bounding_box,
                                                   const plane3* planes,
                                                   plane3* unculled_planes_out,
                                                   size_t* num_planes_inout) {
  TRACE_DURATION("gfx", "PaperShapeCache::CullPlanesAgainstBoundingBox");
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

PaperShapeCacheEntry* PaperShapeCache::FindEntry(const Hash& hash) {
  auto it = cache_.find(hash);
  if (it != cache_.end()) {
    it->second.last_touched_frame = frame_number_;
    return &it->second;
  }
  return nullptr;
}

void PaperShapeCache::AddEntry(const Hash& hash, PaperShapeCacheEntry entry) {
  auto it = cache_.find(hash);
  if (it != cache_.end()) {
    FX_DCHECK(false) << "CacheEntry already exists.";
    return;
  }
  FX_DCHECK(entry.last_touched_frame <= frame_number_);
  entry.last_touched_frame = frame_number_;
  cache_.insert({hash, std::move(entry)});
}

// TODO(fxbug.dev/24173): rather than rolling our own ad-hoc cache eviction strategy,
// (which is already a performance bottleneck) we should plug in a reusable
// cache that performs better and is well-tested.
void PaperShapeCache::TrimCache() {
  TRACE_DURATION("gfx", "PaperShapeCache::TrimCache", "num_entries", cache_.size());
  for (auto it = cache_.begin(); it != cache_.end();) {
    if (frame_number_ - it->second.last_touched_frame >= kNumFramesBeforeEviction) {
      TRACE_DURATION("gfx", "PaperShapeCache::TrimCache[erase]");
      it = cache_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace escher
