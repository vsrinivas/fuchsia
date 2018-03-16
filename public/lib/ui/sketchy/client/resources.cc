// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/sketchy/client/canvas.h"

namespace sketchy_lib {

Resource::Resource(Canvas* canvas)
    : canvas_(canvas), id_(canvas_->AllocateResourceId()) {}

Resource::~Resource() {
  auto release_resource = sketchy::ReleaseResourceCommand::New();
  release_resource->id = id_;
  auto command = sketchy::Command::New();
  command->set_release_resource(std::move(release_resource));
  canvas_->commands_.push_back(std::move(command));
}

void Resource::EnqueueCommand(sketchy::CommandPtr command) const {
  canvas_->commands_.push_back(std::move(command));
}

void Resource::EnqueueCreateResourceCommand(ResourceId resource_id,
                                       sketchy::ResourceArgsPtr args) const {
  auto create_resource = sketchy::CreateResourceCommand::New();
  create_resource->id = resource_id;
  create_resource->args = std::move(args);
  auto command = sketchy::Command::New();
  command->set_create_resource(std::move(create_resource));
  EnqueueCommand(std::move(command));
}

void Resource::EnqueueImportResourceCommand(ResourceId resource_id,
                                       zx::eventpair token,
                                       ui::gfx::ImportSpec spec) const {
  auto import_resource = ui::gfx::ImportResourceCommand::New();
  import_resource->id = resource_id;
  import_resource->token = std::move(token);
  import_resource->spec = spec;
  auto command = sketchy::Command::New();
  command->set_scenic_import_resource(std::move(import_resource));
  EnqueueCommand(std::move(command));
}

Stroke::Stroke(Canvas* canvas) : Resource(canvas) {
  sketchy::StrokePtr stroke = sketchy::Stroke::New();
  auto resource_args = sketchy::ResourceArgs::New();
  resource_args->set_stroke(std::move(stroke));
  EnqueueCreateResourceCommand(id(), std::move(resource_args));
}

void Stroke::SetPath(const StrokePath& path) const {
  auto set_stroke_path = sketchy::SetStrokePathCommand::New();
  set_stroke_path->stroke_id = id();
  set_stroke_path->path = path.NewSketchyStrokePath();
  auto command = sketchy::Command::New();
  command->set_set_path(std::move(set_stroke_path));
  EnqueueCommand(std::move(command));
}

void Stroke::Begin(glm::vec2 pt) const {
  auto begin_stroke = sketchy::BeginStrokeCommand::New();
  begin_stroke->stroke_id = id();
  auto touch = sketchy::Touch::New();
  touch->position = ui::gfx::vec2::New();
  touch->position->x = pt.x;
  touch->position->y = pt.y;
  begin_stroke->touch = std::move(touch);
  auto command = sketchy::Command::New();
  command->set_begin_stroke(std::move(begin_stroke));
  EnqueueCommand(std::move(command));
}

void Stroke::Extend(std::vector<glm::vec2> pts) const {
  auto extend_stroke = sketchy::ExtendStrokeCommand::New();
  extend_stroke->stroke_id = id();
  auto touches = ::f1dl::Array<sketchy::TouchPtr>::New(pts.size());
  for (size_t i = 0; i < pts.size(); i++) {
    touches->at(i) = sketchy::Touch::New();
    touches->at(i)->position = ui::gfx::vec2::New();
    touches->at(i)->position->x = pts[i].x;
    touches->at(i)->position->y = pts[i].y;
  }
  extend_stroke->touches = std::move(touches);
  // TODO(MZ-269): Populate predicted touches.
  extend_stroke->predicted_touches = ::f1dl::Array<sketchy::TouchPtr>::New(0);
  auto command = sketchy::Command::New();
  command->set_extend_stroke(std::move(extend_stroke));
  EnqueueCommand(std::move(command));
}

void Stroke::Finish() const {
  auto finish_stroke = sketchy::FinishStrokeCommand::New();
  finish_stroke->stroke_id = id();
  auto command = sketchy::Command::New();
  command->set_finish_stroke(std::move(finish_stroke));
  EnqueueCommand(std::move(command));
}

StrokeGroup::StrokeGroup(Canvas* canvas) : Resource(canvas) {
  sketchy::StrokeGroupPtr stroke_group = sketchy::StrokeGroup::New();
  auto resource_args = sketchy::ResourceArgs::New();
  resource_args->set_stroke_group(std::move(stroke_group));
  EnqueueCreateResourceCommand(id(), std::move(resource_args));
}

void StrokeGroup::AddStroke(const Stroke& stroke) const {
  auto add_stroke = sketchy::AddStrokeCommand::New();
  add_stroke->stroke_id = stroke.id();
  add_stroke->group_id = id();
  auto command = sketchy::Command::New();
  command->set_add_stroke(std::move(add_stroke));
  EnqueueCommand(std::move(command));
}

void StrokeGroup::RemoveStroke(const Stroke& stroke) const {
  auto remove_stroke = sketchy::RemoveStrokeCommand::New();
  remove_stroke->stroke_id = stroke.id();
  remove_stroke->group_id = id();
  auto command = sketchy::Command::New();
  command->set_remove_stroke(std::move(remove_stroke));
  EnqueueCommand(std::move(command));
}

void StrokeGroup::Clear() const {
  auto clear_group = sketchy::ClearGroupCommand::New();
  clear_group->group_id = id();
  auto command = sketchy::Command::New();
  command->set_clear_group(std::move(clear_group));
  EnqueueCommand(std::move(command));
}

ImportNode::ImportNode(Canvas* canvas, scenic_lib::EntityNode& export_node)
    : Resource(canvas) {
  zx::eventpair token;
  export_node.ExportAsRequest(&token);
  EnqueueImportResourceCommand(id(), std::move(token), ui::gfx::ImportSpec::NODE);
}

void ImportNode::AddChild(const Resource& child) const {
  auto add_child = ui::gfx::AddChildCommand::New();
  add_child->child_id = child.id();
  add_child->node_id = id();
  auto command = sketchy::Command::New();
  command->set_scenic_add_child(std::move(add_child));
  EnqueueCommand(std::move(command));
}

}  // namespace sketchy_lib
