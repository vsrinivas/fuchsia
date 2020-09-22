// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/snapshot/snapshotter.h"

#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmo.h>

#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/vector.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/third_party/granite/vk/command_buffer.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/image.h"
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/gfx/resources/buffer.h"
#include "src/ui/scenic/lib/gfx/resources/camera.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/display_compositor.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/gfx/resources/image.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe_base.h"
#include "src/ui/scenic/lib/gfx/resources/lights/ambient_light.h"
#include "src/ui/scenic/lib/gfx/resources/lights/directional_light.h"
#include "src/ui/scenic/lib/gfx/resources/lights/point_light.h"
#include "src/ui/scenic/lib/gfx/resources/material.h"
#include "src/ui/scenic/lib/gfx/resources/memory.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/entity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/opacity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/scene.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/resources/renderers/renderer.h"
#include "src/ui/scenic/lib/gfx/resources/resource_visitor.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/circle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/mesh_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/rectangle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/rounded_rectangle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"
#include "src/ui/scenic/lib/gfx/snapshot/version.h"

namespace scenic_impl {
namespace gfx {

namespace {

// Color used to replace protected content.
constexpr uint8_t kReplacementImageColor[4] = {0, 0, 0, 255};

// Helper function to create a |SizedVmo| from bytes of certain size.
bool VmoFromBytes(const uint8_t* bytes, size_t num_bytes, uint32_t type, uint32_t version,
                  fsl::SizedVmo* sized_vmo_ptr) {
  FX_CHECK(sized_vmo_ptr);

  zx::vmo vmo;
  size_t total_size = num_bytes + sizeof(type) + sizeof(version);
  zx_status_t status = zx::vmo::create(total_size, 0u, &vmo);
  if (status < 0) {
    FX_PLOGS(WARNING, status) << "zx::vmo::create failed";
    return false;
  }

  if (num_bytes > 0) {
    status = vmo.write(&type, 0, sizeof(uint32_t));
    if (status < 0) {
      FX_PLOGS(WARNING, status) << "zx::vmo::write snapshot type failed";
      return false;
    }
    status = vmo.write(&version, sizeof(uint32_t), sizeof(uint32_t));
    if (status < 0) {
      FX_PLOGS(WARNING, status) << "zx::vmo::write snapshot version failed";
      return false;
    }
    status = vmo.write(bytes, 2 * sizeof(uint32_t), num_bytes);
    if (status < 0) {
      FX_PLOGS(WARNING, status) << "zx::vmo::write bytes failed";
      return false;
    }
  }

  *sized_vmo_ptr = fsl::SizedVmo(std::move(vmo), total_size);

  return true;
}

}  // namespace

escher::ImagePtr Snapshotter::CreateReplacementImage(uint32_t width, uint32_t height) {
  // Lazy creation.
  if (!gpu_uploader_for_replacements_) {
    gpu_uploader_for_replacements_ = escher::BatchGpuUploader::New(escher_);
  }

  // TODO(fxbug.dev/41024): data for a single pixel is provided, but there should be data for width
  // * height pixels.
  return escher_->NewRgbaImage(gpu_uploader_for_replacements_.get(), width, height,
                               const_cast<uint8_t*>(kReplacementImageColor));
}

Snapshotter::Snapshotter(escher::EscherWeakPtr escher)
    : gpu_downloader_(
          escher::BatchGpuDownloader::New(escher, escher::CommandBuffer::Type::kGraphics)),
      escher_(escher) {}

void Snapshotter::TakeSnapshot(Resource* resource, TakeSnapshotCallback snapshot_callback) {
  FX_DCHECK(resource) << "Cannot snapshot null resource.";
  // Visit the scene graph starting with |resource| and collecting images
  // and buffers to read from the GPU.
  resource->Accept(this);

  auto content_ready_callback = [rounded_rect_data_vec = std::move(rounded_rect_data_vec_),
                                 node_serializer = current_node_serializer_,
                                 snapshot_callback = std::move(snapshot_callback)]() {
    TRACE_DURATION("gfx", "Snapshotter::Serialize");
    flatbuffers::FlatBufferBuilder builder;
    builder.Finish(node_serializer->serialize(builder));

    fsl::SizedVmo sized_vmo;
    if (!VmoFromBytes(builder.GetBufferPointer(), builder.GetSize(),
                      SnapshotData::SnapshotType::kFlatBuffer, SnapshotData::SnapshotVersion::v1_0,
                      &sized_vmo)) {
      return snapshot_callback(fuchsia::mem::Buffer{}, false);
    } else {
      return snapshot_callback(std::move(sized_vmo).ToTransport(), true);
    }
  };

  // If we needed to upload any replacement images for protected memory, do that first,
  // and make the "downloading uploader" wait on this upload.
  // TODO(before-41029): would be more efficient to just serialize fake data directly, but
  // that would require significant changes to snapshotter.
  if (gpu_uploader_for_replacements_) {
    FX_DCHECK(gpu_uploader_for_replacements_->HasContentToUpload());
    auto replacement_semaphore = escher::Semaphore::New(escher_->vk_device());
    gpu_uploader_for_replacements_->AddSignalSemaphore(replacement_semaphore);
    gpu_uploader_for_replacements_->Submit();
    gpu_downloader_->AddWaitSemaphore(std::move(replacement_semaphore),
                                      vk::PipelineStageFlagBits::eTransfer);
  }

  // If the Snapshotter has a Engine binding, we need to ensure that the
  // commands in |gpu_downloader_| are executed after commands in the engine's
  // command buffer.
  if (escher_ && escher_->semaphore_chain() && gpu_downloader_->HasContentToDownload()) {
    auto semaphore_pair = escher_->semaphore_chain()->TakeLastAndCreateNextSemaphore();
    gpu_downloader_->AddSignalSemaphore(std::move(semaphore_pair.semaphore_to_signal));
    gpu_downloader_->AddWaitSemaphore(std::move(semaphore_pair.semaphore_to_wait),
                                      vk::PipelineStageFlagBits::eTransfer);
  }

  // The |content_ready_callback| will be always called no matter whether we
  // have contents to download or not.
  gpu_downloader_->Submit(std::move(content_ready_callback));
}

void Snapshotter::Visit(EntityNode* r) { VisitNode(r); }
void Snapshotter::Visit(OpacityNode* r) { VisitNode(r); }
void Snapshotter::Visit(ShapeNode* r) {
  if (r->shape() && r->material()) {
    VisitNode(r);
    r->shape()->Accept(this);
    r->material()->Accept(this);
  }
}

void Snapshotter::Visit(Scene* r) {
  // TODO(fxbug.dev/24424): Should handle Scene better, e.g. storing the lights.
  VisitNode(r);
}

void Snapshotter::Visit(CircleShape* r) {
  FX_DCHECK(current_node_serializer_);
  auto shape = std::make_shared<CircleSerializer>();
  shape->radius = r->radius();

  current_node_serializer_->shape = shape;
}

void Snapshotter::Visit(RectangleShape* r) {
  FX_DCHECK(current_node_serializer_);

  auto shape = std::make_shared<RectangleSerializer>();
  shape->width = r->width();
  shape->height = r->height();

  current_node_serializer_->shape = shape;
}

void Snapshotter::Visit(RoundedRectangleShape* r) {
  FX_DCHECK(current_node_serializer_);

  auto shape = std::make_shared<RoundedRectangleSerializer>();
  shape->width = r->width();
  shape->height = r->height();

  shape->top_left_radius = r->top_left_radius();
  shape->top_right_radius = r->top_right_radius();
  shape->bottom_right_radius = r->bottom_right_radius();
  shape->bottom_left_radius = r->bottom_left_radius();

  current_node_serializer_->shape = shape;

  VisitRoundedRectSpec(r->spec());
}

void Snapshotter::Visit(MeshShape* r) {
  FX_DCHECK(current_node_serializer_);

  current_node_serializer_->shape = std::make_shared<MeshSerializer>();

  VisitMesh(r->escher_mesh());
}

void Snapshotter::Visit(Material* r) {
  if (r->texture_image()) {
    r->texture_image()->Accept(this);
  } else {
    auto color = std::make_shared<ColorSerializer>();
    color->red = r->red();
    color->green = r->green();
    color->blue = r->blue();
    color->alpha = r->alpha();

    current_node_serializer_->material = color;
  }

  VisitResource(r);
}

void Snapshotter::Visit(Memory* r) { VisitResource(r); }

void Snapshotter::Visit(Image* r) {
  VisitImage(r->GetEscherImage());
  VisitResource(r);
}

void Snapshotter::Snapshotter::Visit(ImagePipeBase* r) {
  VisitImage(r->GetEscherImage());
  VisitResource(r);
}
void Snapshotter::Visit(Buffer* r) { VisitResource(r); }
void Snapshotter::Visit(View* r) { VisitResource(r); }
void Snapshotter::Visit(ViewNode* r) { VisitNode(r); }
void Snapshotter::Visit(ViewHolder* r) { VisitNode(r); }
void Snapshotter::Visit(Compositor* r) { r->layer_stack()->Accept(this); }

void Snapshotter::Visit(DisplayCompositor* r) { r->layer_stack()->Accept(this); }
void Snapshotter::Visit(LayerStack* r) {
  for (auto& layer : r->layers()) {
    layer->Accept(this);
  }
}
void Snapshotter::Visit(Layer* r) { r->renderer()->Accept(this); }
void Snapshotter::Visit(Camera* r) { r->scene()->Accept(this); }
void Snapshotter::Visit(Renderer* r) { r->camera()->Accept(this); }
void Snapshotter::Visit(Light* r) { VisitResource(r); }
void Snapshotter::Visit(AmbientLight* r) { VisitResource(r); }
void Snapshotter::Visit(DirectionalLight* r) { VisitResource(r); }
void Snapshotter::Visit(PointLight* r) { VisitResource(r); }

void Snapshotter::VisitNode(Node* r) {
  auto parent_serializer = current_node_serializer_;
  auto node_serializer = std::make_shared<NodeSerializer>();
  if (parent_serializer) {
    parent_serializer->children.push_back(node_serializer);
  }

  // Name.
  node_serializer->name = r->label();

  // Transform.
  if (!r->transform().IsIdentity()) {
    auto transform = std::make_shared<TransformSerializer>();
    node_serializer->transform = transform;

    auto& translation = r->translation();
    transform->translation = snapshot::Vec3(translation.x, translation.y, translation.z);

    auto& scale = r->scale();
    transform->scale = snapshot::Vec3(scale.x, scale.y, scale.z);

    auto& rotation = r->rotation();
    transform->rotation = snapshot::Quat(rotation.x, rotation.y, rotation.z, rotation.w);

    auto& anchor = r->anchor();
    transform->anchor = snapshot::Vec3(anchor.x, anchor.y, anchor.z);
  }

  // Children.
  for (auto& child : r->children()) {
    // Set current node to this node during children traversal.
    current_node_serializer_ = node_serializer;
    child->Accept(this);
  }

  // Set current node to this node during children traversal.
  current_node_serializer_ = node_serializer;
  VisitResource(r);

  current_node_serializer_ = node_serializer;
}

void Snapshotter::VisitResource(Resource* r) {}

void Snapshotter::VisitImage(escher::ImagePtr image) {
  if (!image) {
    return;
  }
  if (image->use_protected_memory()) {
    // We are not allowed to readback protected memory.
    image = CreateReplacementImage(image->width(), image->height());
  }

  auto format = (int32_t)image->format();
  auto width = image->width();
  auto height = image->height();

  ReadImage(image, [format, width, height, node_serializer = current_node_serializer_](
                       const void* host_ptr, size_t size) {
    auto image = std::make_shared<ImageSerializer>(format, width, height, host_ptr, size);
    node_serializer->material = image;
  });
}

void Snapshotter::VisitMesh(escher::MeshPtr mesh) {
  FX_DCHECK(current_node_serializer_);

  auto geometry = std::make_shared<GeometrySerializer>();
  geometry->bbox_min = snapshot::Vec3(mesh->bounding_box().min().x, mesh->bounding_box().min().y,
                                      mesh->bounding_box().min().z);
  geometry->bbox_max = snapshot::Vec3(mesh->bounding_box().max().x, mesh->bounding_box().max().y,
                                      mesh->bounding_box().max().z);
  current_node_serializer_->mesh = geometry;

  for (int i = -1; i < (int)mesh->attribute_buffers().size(); i++) {
    // -1 implies index buffer, >=0 is attribute buffer.
    bool is_index_buffer = i == -1;
    auto src_buffer = is_index_buffer ? mesh->index_buffer() : mesh->attribute_buffer(i).buffer;
    // Attribute buffer other than primarily attribute buffers can be null.
    if (!src_buffer) {
      continue;
    }

    ReadBuffer(src_buffer, [geometry, is_index_buffer, mesh](const void* host_ptr, size_t size) {
      if (is_index_buffer) {
        auto indices = std::make_shared<IndexBufferSerializer>(mesh->num_indices(), host_ptr, size);
        geometry->indices = indices;
      } else {
        auto attribute = std::make_shared<AttributeBufferSerializer>(
            /* vertex_count */ mesh->num_vertices(), /* stride */ mesh->spec().stride(0), host_ptr,
            size);
        geometry->attributes.push_back(attribute);
      }
    });
  }
}

// This function tessellates a new rounded rect mesh and writes out the mesh
// data to the geometry serializer. This avoids having to read in an existing
// GPU mesh buffer.
//
// To ensure that the tessellated mesh data remains alive long enough for it
// to be serialized after this traversal is over, the data is stored in a
// |RoundedRectData| struct which is stored in an array, to be cleared after
// serialization is complete.
void Snapshotter::VisitRoundedRectSpec(const escher::RoundedRectSpec& spec) {
  FX_DCHECK(current_node_serializer_);
  auto geometry = std::make_shared<GeometrySerializer>();

  // Create the mesh spec and make sure that the attribute offsets match those
  // of the PosUvVertex struct. Also make sure that the total stride is equal to
  // to the size of PosUvVertex. Index type sizes must also match.
  const escher::MeshSpec mesh_spec{
      {escher::MeshAttribute::kPosition2D | escher::MeshAttribute::kUV}};
  FX_DCHECK(mesh_spec.attribute_offset(0, escher::MeshAttribute::kPosition2D) ==
            offsetof(PosUvVertex, pos))
      << "Position offsets do not match.";
  FX_DCHECK(mesh_spec.attribute_offset(0, escher::MeshAttribute::kUV) == offsetof(PosUvVertex, uv))
      << "UV offsets do not match.";
  FX_DCHECK(mesh_spec.stride(0) == sizeof(PosUvVertex)) << "Vertex strides do not match.";
  FX_DCHECK(sizeof(escher::MeshSpec::IndexType) == sizeof(uint32_t))
      << "Index type sizes do not match.";

  // Grab the counts for indices.
  uint32_t vertex_count;
  uint32_t index_count;
  std::tie(vertex_count, index_count) = GetRoundedRectMeshVertexAndIndexCounts(spec);

  // Create a new RoundedRectData struct.
  RoundedRectData rect_data;
  rect_data.indices.resize(index_count);
  rect_data.vertices.resize(vertex_count);

  // Calculate the total number of bytes needed for the indices and vertices.
  const uint32_t kIndexBytes = sizeof(escher::MeshSpec::IndexType) * rect_data.indices.size();
  const uint32_t kVertexBytes = sizeof(PosUvVertex) * rect_data.vertices.size();

  // Now actually generate the data for the indices and vertices.
  GenerateRoundedRectIndices(spec, mesh_spec, rect_data.indices.data(), kIndexBytes);
  GenerateRoundedRectVertices(spec, mesh_spec, rect_data.vertices.data(), kVertexBytes);

  // Get the bounding box from the RoundedRectSpec.
  const escher::BoundingBox bounding_box(-0.5f * escher::vec3(spec.width, spec.height, 0),
                                         0.5f * escher::vec3(spec.width, spec.height, 0));

  // Write out the bounding box data to the geometry serializer.
  geometry->bbox_min =
      snapshot::Vec3(bounding_box.min().x, bounding_box.min().y, bounding_box.min().z);
  geometry->bbox_max =
      snapshot::Vec3(bounding_box.max().x, bounding_box.max().y, bounding_box.max().z);
  current_node_serializer_->mesh = geometry;

  // Write out the index data to the geometry serializer.
  geometry->indices =
      std::make_shared<IndexBufferSerializer>(index_count, rect_data.indices.data(), kIndexBytes);

  // Write out the vertex data to the geometry serializer.
  geometry->attributes.push_back(std::make_shared<AttributeBufferSerializer>(
      /* vertex_count */ vertex_count, /* stride */ mesh_spec.stride(0), rect_data.vertices.data(),
      kVertexBytes));

  // Add the rect data to rounded rect vector so it can be kept alive until after serialization
  // is complete. Then the vector will be cleared.
  rounded_rect_data_vec_.push_back(std::move(rect_data));
}

void Snapshotter::ReadImage(const escher::ImagePtr& image,
                            escher::BatchGpuDownloader::Callback callback) {
  gpu_downloader_->ScheduleReadImage(image, std::move(callback));
}

void Snapshotter::ReadBuffer(const escher::BufferPtr& buffer,
                             escher::BatchGpuDownloader::Callback callback) {
  gpu_downloader_->ScheduleReadBuffer(buffer, std::move(callback));
}

}  // namespace gfx
}  // namespace scenic_impl
