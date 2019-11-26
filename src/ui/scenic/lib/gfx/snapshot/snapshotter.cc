// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/snapshot/snapshotter.h"

#include <lib/fit/function.h>
#include <lib/zx/vmo.h>

#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/vector.h"
#include "src/lib/fxl/logging.h"
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
// Helper function to create a |SizedVmo| from bytes of certain size.
bool VmoFromBytes(const uint8_t* bytes, size_t num_bytes, uint32_t type, uint32_t version,
                  fsl::SizedVmo* sized_vmo_ptr) {
  FXL_CHECK(sized_vmo_ptr);

  zx::vmo vmo;
  size_t total_size = num_bytes + sizeof(type) + sizeof(version);
  zx_status_t status = zx::vmo::create(total_size, 0u, &vmo);
  if (status < 0) {
    FXL_PLOG(WARNING, status) << "zx::vmo::create failed";
    return false;
  }

  if (num_bytes > 0) {
    status = vmo.write(&type, 0, sizeof(uint32_t));
    if (status < 0) {
      FXL_PLOG(WARNING, status) << "zx::vmo::write snapshot type failed";
      return false;
    }
    status = vmo.write(&version, sizeof(uint32_t), sizeof(uint32_t));
    if (status < 0) {
      FXL_PLOG(WARNING, status) << "zx::vmo::write snapshot version failed";
      return false;
    }
    status = vmo.write(bytes, 2 * sizeof(uint32_t), num_bytes);
    if (status < 0) {
      FXL_PLOG(WARNING, status) << "zx::vmo::write bytes failed";
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

  // Fuchsia colors
  // TODO(41024): data for a single pixel is provided, but there should be data for width * height
  // pixels.
  uint8_t channels[4];
  channels[1] = 119;
  channels[0] = channels[2] = channels[3] = 255;
  return escher_->NewRgbaImage(gpu_uploader_for_replacements_.get(), width, height, channels);
}

Snapshotter::Snapshotter(escher::EscherWeakPtr escher)
    : gpu_downloader_(
          escher::BatchGpuDownloader::New(escher, escher::CommandBuffer::Type::kGraphics)),
      escher_(escher) {}

void Snapshotter::TakeSnapshot(Resource* resource, TakeSnapshotCallback snapshot_callback) {
  FXL_DCHECK(resource) << "Cannot snapshot null resource.";
  // Visit the scene graph starting with |resource| and collecting images
  // and buffers to read from the GPU.
  resource->Accept(this);

  auto content_ready_callback = [node_serializer = current_node_serializer_,
                                 snapshot_callback = std::move(snapshot_callback)]() {
    TRACE_DURATION("gfx", "Snapshotter::Serialize");
    auto builder = std::make_shared<flatbuffers::FlatBufferBuilder>();
    builder->Finish(node_serializer->serialize(*builder));

    fsl::SizedVmo sized_vmo;
    if (!VmoFromBytes(builder->GetBufferPointer(), builder->GetSize(),
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
    FXL_DCHECK(gpu_uploader_for_replacements_->HasContentToUpload());
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
  // TODO(SCN-1221): Should handle Scene better, e.g. storing the lights.
  VisitNode(r);
}

void Snapshotter::Visit(CircleShape* r) {
  FXL_DCHECK(current_node_serializer_);
  auto shape = std::make_shared<CircleSerializer>();
  shape->radius = r->radius();

  current_node_serializer_->shape = shape;
}

void Snapshotter::Visit(RectangleShape* r) {
  FXL_DCHECK(current_node_serializer_);

  auto shape = std::make_shared<RectangleSerializer>();
  shape->width = r->width();
  shape->height = r->height();

  current_node_serializer_->shape = shape;
}

void Snapshotter::Visit(RoundedRectangleShape* r) {
  FXL_DCHECK(current_node_serializer_);

  auto shape = std::make_shared<RoundedRectangleSerializer>();
  shape->width = r->width();
  shape->height = r->height();

  shape->top_left_radius = r->top_left_radius();
  shape->top_right_radius = r->top_right_radius();
  shape->bottom_right_radius = r->bottom_right_radius();
  shape->bottom_left_radius = r->bottom_left_radius();

  current_node_serializer_->shape = shape;

  VisitMesh(r->escher_mesh());
}

void Snapshotter::Visit(MeshShape* r) {
  FXL_DCHECK(current_node_serializer_);

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
  FXL_DCHECK(current_node_serializer_);

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

void Snapshotter::ReadImage(const escher::ImagePtr& image,
                            escher::BatchGpuDownloader::CallbackType callback) {
  vk::BufferImageCopy region;
  region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = image->width();
  region.imageExtent.height = image->height();
  region.imageExtent.depth = 1;
  region.bufferOffset = 0;

  gpu_downloader_->ScheduleReadImage(image, region, std::move(callback));
}

void Snapshotter::ReadBuffer(const escher::BufferPtr& buffer,
                             escher::BatchGpuDownloader::CallbackType callback) {
  auto region = vk::BufferCopy(/* srcOffset */ 0, /* dstOffset */ 0, /* size */ buffer->size());
  gpu_downloader_->ScheduleReadBuffer(buffer, region, std::move(callback));
}

}  // namespace gfx
}  // namespace scenic_impl
