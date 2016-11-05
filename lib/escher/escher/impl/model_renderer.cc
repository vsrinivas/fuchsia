// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/model_renderer.h"

#include "escher/geometry/tessellation.h"
#include "escher/impl/command_buffer.h"
#include "escher/impl/mesh_impl.h"
#include "escher/impl/mesh_manager.h"
#include "escher/impl/model_data.h"
#include "escher/impl/pipeline.h"
#include "escher/impl/pipeline_cache.h"
#include "escher/scene/model.h"
#include "escher/scene/shape.h"
#include "escher/scene/stage.h"

namespace escher {
namespace impl {

ModelRenderer::ModelRenderer(MeshManager* mesh_manager,
                             ModelData* model_data,
                             PipelineCache* pipeline_cache)
    : mesh_manager_(mesh_manager),
      model_data_(model_data),
      pipeline_cache_(pipeline_cache) {
  rectangle_ = CreateRectangle();
  circle_ = CreateCircle();
}

ModelRenderer::~ModelRenderer() {}

void ModelRenderer::Draw(Stage& stage,
                         Model& model,
                         CommandBuffer* command_buffer) {
  vk::CommandBuffer vk_command_buffer = command_buffer->get();

  auto& objects = model.objects();
  ModelUniformWriter* writer =
      model_data_->GetWriterWithCapacity(command_buffer, objects.size(), 0.2f);

  ModelData::PerModel per_model;
  per_model.brightness = vec4(vec3(stage.brightness()), 1.f);
  writer->WritePerModelData(per_model);

  // Write per-object uniforms, and collect a list of bindings that can be
  // used once the uniforms have been flushed to the GPU.
  FTL_DCHECK(per_object_bindings_.empty());
  {
    // TODO: read screen width from stage.
    constexpr float kHalfWidthRecip = 2.f / 1024.f;
    constexpr float kHalfHeightRecip = 2.f / 1024.f;

    ModelData::PerObject per_object;
    auto& scale_x = per_object.transform[0][0];
    auto& scale_y = per_object.transform[1][1];
    auto& translate_x = per_object.transform[3][0];
    auto& translate_y = per_object.transform[3][1];
    auto& translate_z = per_object.transform[3][2];
    for (const Object& o : objects) {
      // Push uniforms for scale/translation and color.
      scale_x = o.width() * kHalfWidthRecip;
      scale_y = o.height() * kHalfHeightRecip;
      translate_x = o.position().x * kHalfWidthRecip - 1.f;
      translate_y = o.position().y * kHalfHeightRecip - 1.f;
      translate_z = o.position().z;
      per_object.color = vec4(o.color(), 1.f);  // always opaque

      per_object_bindings_.push_back(writer->WritePerObjectData(per_object));
    }
    writer->Flush(vk_command_buffer);
  }

  // Do a second pass over the data, so that flushing the uniform writer above
  // can set a memory barrier before shaders use those uniforms.  This only
  // sucks a litte bit, because we'll eventually want to sort draw calls by
  // pipeline/opacity/depth-order/etc.
  // TODO: sort draw calls.
  PipelineSpec previous_pipeline_spec;
  Pipeline* pipeline = nullptr;
  for (size_t i = 0; i < objects.size(); ++i) {
    const Object& o = objects[i];
    const MeshPtr& mesh = GetMeshForShape(o.shape());
    FTL_DCHECK(mesh);

    PipelineSpec pipeline_spec;
    pipeline_spec.mesh_spec = mesh->spec;

    // Don't rebind pipeline state if it is already up-to-date.
    if (previous_pipeline_spec != pipeline_spec) {
      pipeline = pipeline_cache_->GetPipeline(
          pipeline_spec, mesh_manager_->GetMeshSpecImpl(mesh->spec));

      vk_command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                     pipeline->pipeline());

      // TODO: many pipeline will share the same layout so rebinding may not be
      // necessary.
      writer->BindPerModelData(pipeline->pipeline_layout(), vk_command_buffer);

      previous_pipeline_spec = pipeline_spec;
    }

    // Bind the descriptor set, using the binding obtained in the first pass.
    writer->BindPerObjectData(per_object_bindings_[i],
                              pipeline->pipeline_layout(), vk_command_buffer);

    command_buffer->DrawMesh(mesh);
  }

  per_object_bindings_.clear();
}

const MeshPtr& ModelRenderer::GetMeshForShape(const Shape& shape) const {
  switch (shape.type()) {
    case Shape::Type::kRect:
      return rectangle_;
    case Shape::Type::kCircle:
      return circle_;
    case Shape::Type::kMesh:
      return shape.mesh();
  }
  FTL_CHECK(false);
  return shape.mesh();  // this would DCHECK
}

MeshPtr ModelRenderer::CreateRectangle() {
  // TODO: create rectangle, not triangle.
  MeshSpec spec;
  spec.flags |= MeshAttributeFlagBits::kPosition;
  spec.flags |= MeshAttributeFlagBits::kUV;

  // In each vertex, the first vec2 is position and the second is UV coords.
  ModelData::PerVertex v0{vec2(0.f, 0.f), vec2(0.f, 0.f)};
  ModelData::PerVertex v1{vec2(1.f, 0.f), vec2(1.f, 0.f)};
  ModelData::PerVertex v2{vec2(1.f, 1.f), vec2(1.f, 1.f)};
  ModelData::PerVertex v3{vec2(0.f, 1.f), vec2(0.f, 1.f)};

  MeshBuilderPtr builder = mesh_manager_->NewMeshBuilder(spec, 4, 6);
  return builder->AddVertex(v0)
      .AddVertex(v1)
      .AddVertex(v2)
      .AddVertex(v3)
      .AddIndex(0)
      .AddIndex(1)
      .AddIndex(2)
      .AddIndex(0)
      .AddIndex(2)
      .AddIndex(3)
      .Build();
}

MeshPtr ModelRenderer::CreateCircle() {
  MeshSpec spec;
  spec.flags |= MeshAttributeFlagBits::kPosition;
  spec.flags |= MeshAttributeFlagBits::kUV;

  return TessellateCircle(mesh_manager_, spec, 4, vec2(0.5f, 0.5f), 0.5f);
}

}  // namespace impl
}  // namespace escher
