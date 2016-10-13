// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/impl/model_data.h"
#include "escher/impl/model_uniform_writer.h"
#include "escher/shape/mesh.h"

namespace escher {
namespace impl {

class ModelData;

class ModelRenderer {
 public:
  ModelRenderer(MeshManager* mesh_manager,
                ModelData* model_data,
                PipelineCache* pipeline_cache);
  ~ModelRenderer();
  void Draw(Stage& stage, Model& model, RenderFrame* render_frame);

 private:
  const MeshPtr& GetMeshForShape(const Shape& shape) const;

  MeshManager* mesh_manager_;
  ModelData* model_data_;
  PipelineCache* pipeline_cache_;

  // Avoid per-frame heap allocations.
  std::vector<ModelUniformWriter::PerObjectBinding> per_object_bindings_;

  MeshPtr CreateRectangle();
  MeshPtr CreateCircle();

  MeshPtr rectangle_;
  MeshPtr circle_;
};

}  // namespace impl
}  // namespace escher
