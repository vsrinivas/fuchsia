// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/sketchy/client/canvas.h"

namespace sketchy_lib {

Resource::Resource(Canvas* canvas)
    : canvas_(canvas), id_(canvas_->AllocateResourceId()) {}

Resource::~Resource() {
  ::fuchsia::ui::sketchy::ReleaseResourceCmd release_resource;
  release_resource.id = id_;
  ::fuchsia::ui::sketchy::Command command;
  command.set_release_resource(std::move(release_resource));
  EnqueueCmd(std::move(command));
}

void Resource::EnqueueCmd(::fuchsia::ui::sketchy::Command command) const {
  canvas_->commands_.push_back(std::move(command));
}

void Resource::EnqueueCreateResourceCmd(ResourceId resource_id,
                                            ::fuchsia::ui::sketchy::ResourceArgs args) const {
  ::fuchsia::ui::sketchy::CreateResourceCmd create_resource;
  create_resource.id = resource_id;
  create_resource.args = std::move(args);
  ::fuchsia::ui::sketchy::Command command;
  command.set_create_resource(std::move(create_resource));
  EnqueueCmd(std::move(command));
}

void Resource::EnqueueImportResourceCmd(
    ResourceId resource_id, zx::eventpair token,
    fuchsia::ui::gfx::ImportSpec spec) const {
  fuchsia::ui::gfx::ImportResourceCmd import_resource;
  import_resource.id = resource_id;
  import_resource.token = std::move(token);
  import_resource.spec = spec;
  ::fuchsia::ui::sketchy::Command command;
  command.set_scenic_import_resource(std::move(import_resource));
  EnqueueCmd(std::move(command));
}

Stroke::Stroke(Canvas* canvas) : Resource(canvas) {
  ::fuchsia::ui::sketchy::Stroke stroke;
  ::fuchsia::ui::sketchy::ResourceArgs resource_args;
  resource_args.set_stroke(std::move(stroke));
  EnqueueCreateResourceCmd(id(), std::move(resource_args));
}

void Stroke::SetPath(const StrokePath& path) const {
  ::fuchsia::ui::sketchy::SetStrokePathCmd set_stroke_path;
  set_stroke_path.stroke_id = id();
  set_stroke_path.path = path.NewSketchyStrokePath();
  ::fuchsia::ui::sketchy::Command command;
  command.set_set_path(std::move(set_stroke_path));
  EnqueueCmd(std::move(command));
}

void Stroke::Begin(glm::vec2 pt) const {
  ::fuchsia::ui::sketchy::BeginStrokeCmd begin_stroke;
  begin_stroke.stroke_id = id();
  ::fuchsia::ui::sketchy::Touch touch;
  touch.position.x = pt.x;
  touch.position.y = pt.y;
  begin_stroke.touch = std::move(touch);
  ::fuchsia::ui::sketchy::Command command;
  command.set_begin_stroke(std::move(begin_stroke));
  EnqueueCmd(std::move(command));
}

void Stroke::Extend(std::vector<glm::vec2> pts) const {
  FXL_DCHECK(!pts.empty());

  ::fuchsia::ui::sketchy::ExtendStrokeCmd extend_stroke;
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
  EnqueueCmd(std::move(command));
}

void Stroke::Finish() const {
  ::fuchsia::ui::sketchy::FinishStrokeCmd finish_stroke;
  finish_stroke.stroke_id = id();
  ::fuchsia::ui::sketchy::Command command;
  command.set_finish_stroke(std::move(finish_stroke));
  EnqueueCmd(std::move(command));
}

StrokeGroup::StrokeGroup(Canvas* canvas) : Resource(canvas) {
  ::fuchsia::ui::sketchy::StrokeGroup stroke_group;
  ::fuchsia::ui::sketchy::ResourceArgs resource_args;
  resource_args.set_stroke_group(std::move(stroke_group));
  EnqueueCreateResourceCmd(id(), std::move(resource_args));
}

void StrokeGroup::AddStroke(const Stroke& stroke) const {
  ::fuchsia::ui::sketchy::AddStrokeCmd add_stroke;
  add_stroke.stroke_id = stroke.id();
  add_stroke.group_id = id();
  ::fuchsia::ui::sketchy::Command command;
  command.set_add_stroke(std::move(add_stroke));
  EnqueueCmd(std::move(command));
}

void StrokeGroup::RemoveStroke(const Stroke& stroke) const {
  ::fuchsia::ui::sketchy::RemoveStrokeCmd remove_stroke;
  remove_stroke.stroke_id = stroke.id();
  remove_stroke.group_id = id();
  ::fuchsia::ui::sketchy::Command command;
  command.set_remove_stroke(std::move(remove_stroke));
  EnqueueCmd(std::move(command));
}

void StrokeGroup::Clear() const {
  ::fuchsia::ui::sketchy::ClearGroupCmd clear_group;
  clear_group.group_id = id();
  ::fuchsia::ui::sketchy::Command command;
  command.set_clear_group(std::move(clear_group));
  EnqueueCmd(std::move(command));
}

ImportNode::ImportNode(Canvas* canvas, scenic::EntityNode& export_node)
    : Resource(canvas) {
  zx::eventpair token;
  export_node.ExportAsRequest(&token);
  EnqueueImportResourceCmd(id(), std::move(token),
                               fuchsia::ui::gfx::ImportSpec::NODE);
}

void ImportNode::AddChild(const Resource& child) const {
  fuchsia::ui::gfx::AddChildCmd add_child;
  add_child.child_id = child.id();
  add_child.node_id = id();
  ::fuchsia::ui::sketchy::Command command;
  command.set_scenic_add_child(std::move(add_child));
  EnqueueCmd(std::move(command));
}

}  // namespace sketchy_lib
