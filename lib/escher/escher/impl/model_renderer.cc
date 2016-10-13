// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/model_renderer.h"

// TODO: may not be needed once tessellation is moved out.
#include "escher/geometry/types.h"

#include "escher/impl/mesh_impl.h"
#include "escher/impl/mesh_manager.h"
#include "escher/impl/model_data.h"
#include "escher/impl/pipeline.h"
#include "escher/impl/pipeline_cache.h"
#include "escher/impl/render_frame.h"
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

void ModelRenderer::Draw(Stage& stage, Model& model, RenderFrame* frame) {
  vk::CommandBuffer command_buffer = frame->command_buffer();

  auto& objects = model.objects();
  ModelUniformWriter* writer =
      model_data_->GetWriterWithCapacity(frame, objects.size(), 0.2f);

  ModelData::PerModel per_model;
  per_model.brightness = vec4(vec3(stage.brightness()), 1.f);
  writer->WritePerModelData(per_model);

  // Write per-object uniforms, and collect a list of bindings that can be
  // used once the uniforms have been flushed to the GPU.
  FTL_DCHECK(per_object_bindings_.empty());
  {
    ModelData::PerObject per_object;
    auto& scale_x = per_object.transform[0][0];
    auto& scale_y = per_object.transform[1][1];
    auto& translate_x = per_object.transform[3][0];
    auto& translate_y = per_object.transform[3][1];
    auto& r = per_object.color[0];
    auto& g = per_object.color[1];
    auto& b = per_object.color[2];
    per_object.color[3] = 1.f;  // always opaque
    for (const Object& o : objects) {
      // Push uniforms for scale/translation and color.
      scale_x = o.scale;
      scale_y = o.scale;
      translate_x = o.x;
      translate_y = o.y;
      r = o.red;
      g = o.green;
      b = o.blue;
      per_object_bindings_.push_back(writer->WritePerObjectData(per_object));
    }
    writer->Flush(command_buffer);
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

      command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                  pipeline->pipeline());

      // TODO: many pipeline will share the same layout so rebinding may not be
      // necessary.
      writer->BindPerModelData(pipeline->pipeline_layout(), command_buffer);

      previous_pipeline_spec = pipeline_spec;
    }

    // Bind the descriptor set, using the binding obtained in the first pass.
    writer->BindPerObjectData(per_object_bindings_[i],
                              pipeline->pipeline_layout(), command_buffer);

    frame->DrawMesh(mesh);
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
  spec.flags |= MeshAttributeFlagBits::kColor;

  ModelData::ColorVertex v0{vec2(-0.5, -0.5), vec3(1.0, 0.0, 0.0)};
  ModelData::ColorVertex v1{vec2(0.5, 0.5), vec3(0.0, 0.0, 1.0)};
  ModelData::ColorVertex v2{vec2(-0.5, 0.5), vec3(0.0, 1.0, 0.0)};

  MeshBuilderPtr builder = mesh_manager_->NewMeshBuilder(spec, 6, 12);
  return builder->AddVertex(v0)
      .AddVertex(v1)
      .AddVertex(v2)
      .AddIndex(0)
      .AddIndex(1)
      .AddIndex(2)
      .Build();
}

MeshPtr ModelRenderer::CreateCircle() {
  // TODO: create circle, not a rectangle (erm.. triangle).
  return CreateRectangle();
}

}  // namespace impl
}  // namespace escher
