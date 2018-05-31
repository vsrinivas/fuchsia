// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/sketchy/client/canvas.h"

namespace sketchy_lib {

Resource::Resource(Canvas* canvas)
    : canvas_(canvas), id_(canvas_->AllocateResourceId()) {}

Resource::~Resource() {
  ::fuchsia::ui::sketchy::ReleaseResourceCommand release_resource;
  release_resource.id = id_;
  ::fuchsia::ui::sketchy::Command command;
  command.set_release_resource(std::move(release_resource));
  EnqueueCommand(std::move(command));
}

void Resource::EnqueueCommand(::fuchsia::ui::sketchy::Command command) const {
  canvas_->commands_.push_back(std::move(command));
}

void Resource::EnqueueCreateResourceCommand(ResourceId resource_id,
                                            ::fuchsia::ui::sketchy::ResourceArgs args) const {
  ::fuchsia::ui::sketchy::CreateResourceCommand create_resource;
  create_resource.id = resource_id;
  create_resource.args = std::move(args);
  ::fuchsia::ui::sketchy::Command command;
  command.set_create_resource(std::move(create_resource));
  EnqueueCommand(std::move(command));
}

void Resource::EnqueueImportResourceCommand(
    ResourceId resource_id, zx::eventpair token,
    fuchsia::ui::gfx::ImportSpec spec) const {
  fuchsia::ui::gfx::ImportResourceCommand import_resource;
  import_resource.id = resource_id;
  import_resource.token = std::move(token);
  import_resource.spec = spec;
  ::fuchsia::ui::sketchy::Command command;
  command.set_scenic_import_resource(std::move(import_resource));
  EnqueueCommand(std::move(command));
}

Stroke::Stroke(Canvas* canvas) : Resource(canvas) {
  ::fuchsia::ui::sketchy::Stroke stroke;
  ::fuchsia::ui::sketchy::ResourceArgs resource_args;
  resource_args.set_stroke(std::move(stroke));
  EnqueueCreateResourceCommand(id(), std::move(resource_args));
}

void Stroke::SetPath(const StrokePath& path) const {
  ::fuchsia::ui::sketchy::SetStrokePathCommand set_stroke_path;
  set_stroke_path.stroke_id = id();
  set_stroke_path.path = path.NewSketchyStrokePath();
  ::fuchsia::ui::sketchy::Command command;
  command.set_set_path(std::move(set_stroke_path));
  EnqueueCommand(std::move(command));
}

void Stroke::Begin(glm::vec2 pt) const {
  ::fuchsia::ui::sketchy::BeginStrokeCommand begin_stroke;
  begin_stroke.stroke_id = id();
  ::fuchsia::ui::sketchy::Touch touch;
  touch.position.x = pt.x;
  touch.position.y = pt.y;
  begin_stroke.touch = std::move(touch);
  ::fuchsia::ui::sketchy::Command command;
  command.set_begin_stroke(std::move(begin_stroke));
  EnqueueCommand(std::move(command));
}

void Stroke::Extend(std::vector<glm::vec2> pts) const {
  FXL_DCHECK(!pts.empty());

  ::fuchsia::ui::sketchy::ExtendStrokeCommand extend_stroke;
  extend_stroke.stroke_id = id();
  fidl::VectorPtr<::fuchsia::ui::sketchy::Touch> touches;
  for (size_t i = 0; i < pts.size(); i++) {
    ::fuchsia::ui::sketchy::Touch touch;
    touch.position.x = pts[i].x;
    touch.position.y = pts[i].y;
    touches.push_back(touch);
  }
  extend_stroke.touches = std::move(touches);
  extend_stroke.predicted_touches.resize(0);

  // TODO(MZ-269): Populate predicted touches.
  ::fuchsia::ui::sketchy::Command command;
  command.set_extend_stroke(std::move(extend_stroke));
  EnqueueCommand(std::move(command));
}

void Stroke::Finish() const {
  ::fuchsia::ui::sketchy::FinishStrokeCommand finish_stroke;
  finish_stroke.stroke_id = id();
  ::fuchsia::ui::sketchy::Command command;
  command.set_finish_stroke(std::move(finish_stroke));
  EnqueueCommand(std::move(command));
}

StrokeGroup::StrokeGroup(Canvas* canvas) : Resource(canvas) {
  ::fuchsia::ui::sketchy::StrokeGroup stroke_group;
  ::fuchsia::ui::sketchy::ResourceArgs resource_args;
  resource_args.set_stroke_group(std::move(stroke_group));
  EnqueueCreateResourceCommand(id(), std::move(resource_args));
}

void StrokeGroup::AddStroke(const Stroke& stroke) const {
  ::fuchsia::ui::sketchy::AddStrokeCommand add_stroke;
  add_stroke.stroke_id = stroke.id();
  add_stroke.group_id = id();
  ::fuchsia::ui::sketchy::Command command;
  command.set_add_stroke(std::move(add_stroke));
  EnqueueCommand(std::move(command));
}

void StrokeGroup::RemoveStroke(const Stroke& stroke) const {
  ::fuchsia::ui::sketchy::RemoveStrokeCommand remove_stroke;
  remove_stroke.stroke_id = stroke.id();
  remove_stroke.group_id = id();
  ::fuchsia::ui::sketchy::Command command;
  command.set_remove_stroke(std::move(remove_stroke));
  EnqueueCommand(std::move(command));
}

void StrokeGroup::Clear() const {
  ::fuchsia::ui::sketchy::ClearGroupCommand clear_group;
  clear_group.group_id = id();
  ::fuchsia::ui::sketchy::Command command;
  command.set_clear_group(std::move(clear_group));
  EnqueueCommand(std::move(command));
}

ImportNode::ImportNode(Canvas* canvas, scenic_lib::EntityNode& export_node)
    : Resource(canvas) {
  zx::eventpair token;
  export_node.ExportAsRequest(&token);
  EnqueueImportResourceCommand(id(), std::move(token),
                               fuchsia::ui::gfx::ImportSpec::NODE);
}

void ImportNode::AddChild(const Resource& child) const {
  fuchsia::ui::gfx::AddChildCommand add_child;
  add_child.child_id = child.id();
  add_child.node_id = id();
  ::fuchsia::ui::sketchy::Command command;
  command.set_scenic_add_child(std::move(add_child));
  EnqueueCommand(std::move(command));
}

}  // namespace sketchy_lib
