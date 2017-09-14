// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/sketchy/canvas.h"

namespace sketchy_lib {

Resource::Resource(Canvas* canvas)
    : canvas_(canvas), id_(canvas_->AllocateResourceId()) {}

Resource::~Resource() {
  auto release_resource = sketchy::ReleaseResourceOp::New();
  release_resource->id = id_;
  auto op = sketchy::Op::New();
  op->set_release_resource(std::move(release_resource));
  canvas_->ops_.push_back(std::move(op));
}

void Resource::EnqueueOp(sketchy::OpPtr op) {
  canvas_->ops_.push_back(std::move(op));
}

void Resource::EnqueueCreateResourceOp(ResourceId resource_id,
                                       sketchy::ResourceArgsPtr args) {
  auto create_resource = sketchy::CreateResourceOp::New();
  create_resource->id = resource_id;
  create_resource->args = std::move(args);
  auto op = sketchy::Op::New();
  op->set_create_resource(std::move(create_resource));
  EnqueueOp(std::move(op));
}

void Resource::EnqueueImportResourceOp(ResourceId resource_id,
                                       zx::eventpair token,
                                       scenic::ImportSpec spec) {
  auto import_resource = scenic::ImportResourceOp::New();
  import_resource->id = resource_id;
  import_resource->token = std::move(token);
  import_resource->spec = spec;
  auto op = sketchy::Op::New();
  op->set_scenic_import_resource(std::move(import_resource));
  EnqueueOp(std::move(op));
}

Stroke::Stroke(Canvas* canvas) : Resource(canvas) {
  sketchy::StrokePtr stroke = sketchy::Stroke::New();
  auto resource_args = sketchy::ResourceArgs::New();
  resource_args->set_stroke(std::move(stroke));
  EnqueueCreateResourceOp(id(), std::move(resource_args));
}

StrokeGroup::StrokeGroup(Canvas* canvas) : Resource(canvas) {
  sketchy::StrokeGroupPtr stroke_group = sketchy::StrokeGroup::New();
  auto resource_args = sketchy::ResourceArgs::New();
  resource_args->set_stroke_group(std::move(stroke_group));
  EnqueueCreateResourceOp(id(), std::move(resource_args));
}

void StrokeGroup::AddStroke(Stroke& stroke) {
  auto add_stroke = sketchy::AddStrokeOp::New();
  add_stroke->stroke_id = stroke.id();
  add_stroke->group_id = id();
  auto op = sketchy::Op::New();
  op->set_add_stroke(std::move(add_stroke));
  EnqueueOp(std::move(op));
}

ImportNode::ImportNode(Canvas* canvas, scenic_lib::EntityNode& export_node)
    : Resource(canvas) {
  zx::eventpair token;
  export_node.ExportAsRequest(&token);
  EnqueueImportResourceOp(id(), std::move(token), scenic::ImportSpec::NODE);
}

void ImportNode::AddChild(const Resource& child) {
  auto add_child = scenic::AddChildOp::New();
  add_child->child_id = child.id();
  add_child->node_id = id();
  auto op = sketchy::Op::New();
  op->set_scenic_add_child(std::move(add_child));
  EnqueueOp(std::move(op));
}

}  // namespace sketchy_lib
