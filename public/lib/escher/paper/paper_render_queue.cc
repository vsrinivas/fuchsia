// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/paper/paper_render_queue.h"

#include <glm/gtc/matrix_access.hpp>

#include "lib/escher/escher.h"
#include "lib/escher/geometry/tessellation.h"
#include "lib/escher/impl/mesh_manager.h"
#include "lib/escher/paper/paper_render_funcs.h"
#include "lib/escher/renderer/frame.h"
#include "lib/escher/scene/model.h"
#include "lib/escher/scene/object.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/util/hasher.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/shader_program.h"

namespace escher {

namespace {

// TODO(ES-102): should be queried from device.
constexpr vk::DeviceSize kMinUniformBufferOffsetAlignment = 256;

TexturePtr CreateWhiteTexture(Escher* escher) {
  uint8_t channels[4];
  channels[0] = channels[1] = channels[2] = channels[3] = 255;
  auto image = escher->NewRgbaImage(1, 1, channels);
  return escher->NewTexture(std::move(image), vk::Filter::eNearest);
}

}  // anonymous namespace

PaperRenderQueue::PaperRenderQueue(EscherWeakPtr escher,
                                   ShaderProgramPtr program)
    : escher_(std::move(escher)),
      program_(std::move(program)),
      rectangle_(NewSimpleRectangleMesh(escher_->mesh_manager())),
      circle_(NewCircleMesh(escher_->mesh_manager(),
                            {MeshAttribute::kPosition2D | MeshAttribute::kUV},
                            4, vec2(0, 0), 1)),
      white_texture_(CreateWhiteTexture(escher_.get())),
      view_projection_uniform_{} {}

void PaperRenderQueue::InitFrame(const FramePtr& frame, const Camera& camera) {
  FXL_DCHECK(!frame_ && view_projection_uniform_.buffer == nullptr);
  frame_ = frame;

  view_projection_uniform_ = frame->AllocateUniform(
      sizeof(glm::mat4), kMinUniformBufferOffsetAlignment);
  view_projection_uniform_.as_ref<glm::mat4>() =
      camera.projection() * camera.transform();

  // A camera's transform doesn't move the camera; it is applied to the rest of
  // the scene to "move it away from the camera".  Therefore, the camera's
  // position in the scene can be obtained by inverting it and applying it to
  // the origin, or equivalently by inverting the transform and taking the
  // rightmost (translation) column.
  camera_pos_ = vec3(glm::column(glm::inverse(camera.transform()), 3));

  // The camera points down the negative-Z axis, so its world-space direction
  // can be obtained by applying the camera transform to the direction vector
  // [0, 0, -1, 0] (remembering that directions vectors have a w-coord of 0, vs.
  // 1 for postion vectors).  This is equivalent to taking the negated third
  // column of the transform.
  camera_dir_ = -vec3(glm::column(camera.transform(), 2));
}

void PaperRenderQueue::Clear() {
  TRACE_DURATION("gfx", "escher::PaperRenderQueue::Clear");
  FXL_DCHECK(frame_ && view_projection_uniform_.host_ptr);

  frame_ = nullptr;
  view_projection_uniform_ = {};

  opaque_.clear();
  translucent_.clear();

  object_data_.clear();
}

void PaperRenderQueue::Sort() {
  TRACE_DURATION("gfx", "escher::PaperRenderQueue::Sort");
  opaque_.Sort();
  translucent_.Sort();
}

void PaperRenderQueue::GenerateCommands(
    CommandBuffer* cb, const CommandBuffer::SavedState* state) const {
  TRACE_DURATION("gfx", "escher::PaperRenderQueue::GenerateCommands");

  // Generate commands to render opaque objects.
  cb->SetToDefaultState(CommandBuffer::DefaultState::kOpaque);
  opaque_.GenerateCommands(cb, state);

  // Generate commands to render translucent objects.
  cb->SetBlendEnable(true);
  cb->SetBlendFactors(
      vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusDstAlpha,
      vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendFactor::eOne);
  cb->SetBlendOp(vk::BlendOp::eAdd);
  translucent_.GenerateCommands(cb, state);
}

void PaperRenderQueue::PushObject(const Object& obj) {
  TRACE_DURATION("gfx", "escher::PaperRenderQueue::Object");
  FXL_DCHECK(frame_);

  auto material = obj.material();
  if (!material)
    return;

  FXL_DCHECK(!obj.shape().modifiers());

  MeshPtr mesh;

  switch (obj.shape().type()) {
    case Shape::Type::kRect: {
      mesh = rectangle_;
    } break;
    case Shape::Type::kCircle: {
      mesh = circle_;
    } break;
    case Shape::Type::kMesh: {
      mesh = obj.shape().mesh();
    } break;
    case Shape::Type::kNone: {
    } break;
  }

  if (!mesh)
    return;

  auto& texture = material->texture() ? material->texture() : white_texture_;

  Hasher h;

  // Only the program goes into the pipeline hash.  If we also wanted e.g. some
  // objects to be stencil-tested and others not, this info would be included.
  h.u64(program_->uid());
  Hash pipeline_hash = h.value();

  // The object-hash is used to look up an existing MeshObjectData for this
  // Mesh/Material pair, and is also used as part of the sort-key below.
  // We don't need to take opacity into account because separate RenderQueues
  // are used for opaque vs. translucent objects.
  h.u64(mesh->uid());
  h.u64(texture->uid());
  Hash object_hash = h.value();

  PaperRenderFuncs::MeshObjectData* obj_data;
  auto it = object_data_.find(object_hash);
  if (it != object_data_.end()) {
    obj_data = reinterpret_cast<PaperRenderFuncs::MeshObjectData*>(it->second);
  } else {
    obj_data = PaperRenderFuncs::NewMeshObjectData(
        frame_, mesh, texture, program_, view_projection_uniform_);
    object_data_.insert(it, std::make_pair(object_hash, obj_data));
  }

  // Allocate and initialize per-instance data.
  PaperRenderFuncs::MeshInstanceData* inst_data =
      frame_->Allocate<PaperRenderFuncs::MeshInstanceData>();

  // Matches ObjectProperties in simple.vert
  struct ObjectProperties {
    mat4 model_transform;
    vec4 color;
  };

  UniformAllocation props = frame_->AllocateUniform(
      sizeof(ObjectProperties), kMinUniformBufferOffsetAlignment);
  auto& object_properties = props.as_ref<ObjectProperties>();
  object_properties.model_transform = obj.transform();
  object_properties.color = material->color();
  inst_data->object_properties =
      PaperRenderFuncs::UniformBinding{.descriptor_set_index = 1,
                                       .binding_index = 0,
                                       .buffer = props.buffer,
                                       .offset = props.offset,
                                       .size = props.size};

  // Compute a depth metric for sorting objects.
#if 1
  // As long as the camera is above the top of the viewing volume and the scene
  // is composed of parallel-planar surfaces, we can simply subtract the
  // object's elevation from the camera's elevation.  Given these constraints,
  // this metric is superior to the alternate one below, which can provide
  // incorrect results at glancing angles (i.e. where the center of one object
  // is closer to the camera than the other, but is nevertheless partly behind
  // the other object from the camera's perspective.
  float depth = camera_pos_.z - obj.transform()[3][2];
#else
  // Compute the vector from the camera to the object, and project it against
  // the camera's direction to obtain the depth.
  float depth = glm::dot(vec3(obj.transform()[3]) - camera_pos_, camera_dir_);
#endif

  if (material->opaque()) {
    opaque_.Push(SortKey::NewOpaque(pipeline_hash, object_hash, depth).key(),
                 obj_data, inst_data, PaperRenderFuncs::RenderMesh);
  } else {
    translucent_.Push(
        SortKey::NewTranslucent(pipeline_hash, object_hash, depth).key(),
        obj_data, inst_data, PaperRenderFuncs::RenderMesh);
  }
}

void PaperRenderQueue::PushModel(const Model& model) {
  TRACE_DURATION("gfx", "escher::PaperRenderQueue::PushModel");
  for (auto& obj : model.objects()) {
    PushObject(obj);
  }
}

PaperRenderQueue::SortKey PaperRenderQueue::SortKey::NewOpaque(
    Hash pipeline_hash, Hash draw_hash, float depth) {
  // Depth must be non-negative, otherwise comparing the bit representations
  // won't work.
  FXL_DCHECK(depth >= 0);

  // Prioritize minimizing pipeline changes over depth-sorting; both are more
  // important than minimizing mesh/texture state changes (in practice, almost
  // every draw call uses a separate mesh/texture anyway).
  // TODO(ES-105): We currently don't have multiple pipelines used in the opaque
  // pass, so we sort primarily by depth.  However, when we eventually do have
  // multiple pipelines, we may want to rewrite the pipeline hashes with a value
  // that reflects whether objects drawn using that pipeline tend to be drawn
  // in front or back.  For example, if we wanted to draw the background with a
  // shiny metallic BRDF, and everything else with a matte diffuse shader then
  // we should definitely draw the background last to avoid drawing expensive
  // pixels that will later be overdrawn; this isn't guaranteed if we sort by
  // the pipeline hash.
  uint64_t depth_key(glm::floatBitsToUint(depth));
  return SortKey((pipeline_hash.val << 48) | depth_key << 16 |
                 (draw_hash.val & 0xffff));
}

PaperRenderQueue::SortKey PaperRenderQueue::SortKey::NewTranslucent(
    Hash pipeline_hash, Hash draw_hash, float depth) {
  // Depth must be non-negative, otherwise comparing the bit representations
  // won't work.
  FXL_DCHECK(depth >= 0);

  // Prioritize back-to-front order over state changes.
  uint64_t depth_key(glm::floatBitsToUint(depth) ^ 0xffffffffu);
  return SortKey((depth_key << 32) | (pipeline_hash.val & 0xffff0000u) |
                 (draw_hash.val & 0xffffu));
}

}  // namespace escher
