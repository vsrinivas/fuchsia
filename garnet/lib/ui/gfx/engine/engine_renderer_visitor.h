// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_ENGINE_RENDERER_VISITOR_H_
#define GARNET_LIB_UI_GFX_ENGINE_ENGINE_RENDERER_VISITOR_H_

#include "garnet/lib/ui/gfx/resources/resource_visitor.h"

#include "src/ui/lib/escher/paper/paper_readme.h"

namespace scenic_impl {
namespace gfx {

class Node;

// EngineRendererVisitor is used by EngineRender to traverse a Scene, drawing it
// via PaperRenderer.
//
// EngineRendererVisitor's user is responsible for guaranteeing the lifetime of
// the |renderer| and |gpu_uploader|, as well as for invoking
// PaperRenderer::Begin/EndFrame() and BatchGpuUploader::Submit().
//
// This class is currently designed for one-time use, and is typically destroyed
// immediately afterward.
class EngineRendererVisitor : public ResourceVisitor {
 public:
  // Both the renderer and gpu_uploader must outlive this visitor.
  EngineRendererVisitor(escher::PaperRenderer* renderer,
                        escher::BatchGpuUploader* gpu_uploader);

  // Main entry point.
  // TODO(SCN-1256): EngineRenderer should visit the whole scene-graph, not just
  // a single Scene.  In this case, the class comment would need to be modified,
  // because this would be responsible for calling BeginFrame()/EndFrame, etc.
  void Visit(Scene* r) override;

  void Visit(Memory* r) override;
  void Visit(Image* r) override;
  void Visit(ImagePipe* r) override;
  void Visit(Buffer* r) override;
  void Visit(View* r) override;
  void Visit(ViewNode* r) override;
  void Visit(ViewHolder* r) override;
  void Visit(EntityNode* r) override;
  void Visit(OpacityNode* r) override;
  void Visit(ShapeNode* r) override;
  void Visit(CircleShape* r) override;
  void Visit(RectangleShape* r) override;
  void Visit(RoundedRectangleShape* r) override;
  void Visit(MeshShape* r) override;
  void Visit(Material* r) override;
  void Visit(Compositor* r) override;
  void Visit(DisplayCompositor* r) override;
  void Visit(LayerStack* r) override;
  void Visit(Layer* r) override;
  void Visit(Camera* r) override;
  void Visit(Renderer* r) override;
  void Visit(Light* r) override;
  void Visit(AmbientLight* r) override;
  void Visit(DirectionalLight* r) override;
  void Visit(PointLight* r) override;
  void Visit(Import* r) override;

 private:
  // Visits a node and all it's children.
  void VisitNode(Node* r);

  // Track the opacity level resulting from traversing OpacityNodes.  This
  // opacity is combined with the opacity of each draw call's material.
  float opacity_ = 1.f;

  // Number of times that PaperRenderer::Draw*() methods were invoked.
  size_t draw_call_count_ = 0;

  escher::PaperRenderer* const renderer_;
  escher::BatchGpuUploader* const gpu_uploader_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_ENGINE_RENDERER_VISITOR_H_
