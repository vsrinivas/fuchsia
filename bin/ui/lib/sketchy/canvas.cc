// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/sketchy/canvas.h"

namespace sketchy_lib {

Canvas::Canvas(app::ApplicationContext* context)
    : Canvas(context->ConnectToEnvironmentService<sketchy::Canvas>()) {}

Canvas::Canvas(sketchy::CanvasPtr canvas)
    : canvas_(std::move(canvas)),
      resources_(std::make_unique<ResourceManager>(this)) {
  canvas_.set_connection_error_handler([this] {
    FTL_LOG(INFO)
    << "sketchy_lib::Canvas: lost connection to sketchy::Canvas.";
    mtl::MessageLoop::GetCurrent()->QuitNow();
  });
}

ResourceId Canvas::ImportNode(mozart::client::EntityNode* node) {
  mx::eventpair token;
  ResourceId node_id = resources_->CreateAnonymousResource();
  node->ExportAsRequest(&token);
  auto import_resource = mozart2::ImportResourceOp::New();
  import_resource->id = node_id;
  import_resource->token = std::move(token);
  import_resource->spec = mozart2::ImportSpec::NODE;
  auto op = sketchy::Op::New();
  op->set_scenic_import_resource(std::move(import_resource));
  ops_.push_back(std::move(op));
  return node_id;
}

void Canvas::AddStrokeToNode(
    ResourceId stroke_id, ResourceId node_id) {
  AddChildToNode(stroke_id, node_id);
}

void Canvas::AddStrokeToGroup(
    ResourceId stroke_id, ResourceId group_id) {
  auto add_stroke = sketchy::AddStrokeOp::New();
  add_stroke->stroke_id = stroke_id;
  add_stroke->group_id = group_id;
  auto op = sketchy::Op::New();
  op->set_add_stroke(std::move(add_stroke));
  ops_.push_back(std::move(op));
}

void Canvas::AddStrokeGroupToNode(
    ResourceId group_id, ResourceId node_id) {
  AddChildToNode(group_id, node_id);
}

inline void Canvas::AddChildToNode(
    ResourceId child_id, ResourceId node_id) {
  auto add_child = mozart2::AddChildOp::New();
  add_child->child_id = child_id;
  add_child->node_id = node_id;
  auto op = sketchy::Op::New();
  op->set_scenic_add_child(std::move(add_child));
  ops_.push_back(std::move(op));
}

void Canvas::Present(uint64_t time) {
  if (!ops_.empty()) {
    canvas_->Enqueue(std::move(ops_));
  }
  // TODO: Use this callback to drive Present loop.
  canvas_->Present(time, [](mozart2::PresentationInfoPtr info) {});
}

}  // namespace sketchy_lib
