// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/canvas.h"

#include "garnet/bin/ui/sketchy/frame.h"
#include "garnet/bin/ui/sketchy/resources/import_node.h"
#include "garnet/bin/ui/sketchy/resources/stroke.h"
#include "lib/escher/util/fuchsia_utils.h"

namespace sketchy_service {

CanvasImpl::CanvasImpl(async::Loop* loop, scenic::Session* session,
                       escher::EscherWeakPtr weak_escher)
    : loop_(loop),
      session_(session),
      shared_buffer_pool_(session, weak_escher),
      stroke_manager_(std::move(weak_escher)) {}

void CanvasImpl::Init(
    fidl::InterfaceHandle<::fuchsia::ui::sketchy::CanvasListener> listener) {
  // TODO(MZ-269): unimplemented.
  FXL_LOG(ERROR) << "Init: unimplemented.";
}

void CanvasImpl::Enqueue(
    fidl::VectorPtr<::fuchsia::ui::sketchy::Command> commands) {
  // TODO: Use `AddAll()` when fidl::VectorPtr supports it.
  for (size_t i = 0; i < commands->size(); ++i) {
    commands_.push_back(std::move(commands->at(i)));
  }
}

void CanvasImpl::Present(uint64_t presentation_time, PresentCallback callback) {
  // TODO(MZ-269): Present() should behave the same way as Scenic. Specifically,
  // Commands shouldn't be applied immediately. Instead a frame-request should
  // be triggered and the Commands enqueue; when the corresponding frame is
  // processed all Commands that are scheduled for the current frame's
  // presentation time are applied.
  for (auto it = commands_->begin(); it != commands_->end(); ++it) {
    if (!ApplyCommand(std::move(*it))) {
      loop_->Quit();
    }
  }
  commands_.reset();
  callbacks_.push_back(std::move(callback));
  RequestScenicPresent(presentation_time);
}

void CanvasImpl::RequestScenicPresent(uint64_t presentation_time) {
  if (is_scenic_present_requested_) {
    return;
  }
  is_scenic_present_requested_ = true;

  auto session_callback = [this, callbacks = std::move(callbacks_)](
                              fuchsia::images::PresentationInfo info) {
    FXL_DCHECK(is_scenic_present_requested_);
    is_scenic_present_requested_ = false;
    for (auto& callback : callbacks) {
      callback(info);
    }
    RequestScenicPresent(info.presentation_time + info.presentation_interval);
  };
  callbacks_.clear();

  auto frame = Frame(&shared_buffer_pool_);
  if (frame.init_failed()) {
    session_->Present(presentation_time, std::move(session_callback));
    return;
  }

  stroke_manager_.Update(&frame);
  frame.RequestScenicPresent(session_, presentation_time,
                             std::move(session_callback));
}

bool CanvasImpl::ApplyCommand(::fuchsia::ui::sketchy::Command command) {
  switch (command.Which()) {
    case ::fuchsia::ui::sketchy::Command::Tag::kCreateResource:
      return ApplyCreateResourceCmd(std::move(command.create_resource()));
    case ::fuchsia::ui::sketchy::Command::Tag::kReleaseResource:
      return ApplyReleaseResourceCmd(std::move(command.release_resource()));
    case ::fuchsia::ui::sketchy::Command::Tag::kSetPath:
      return ApplySetPathCmd(std::move(command.set_path()));
    case ::fuchsia::ui::sketchy::Command::Tag::kAddStroke:
      return ApplyAddStrokeCmd(std::move(command.add_stroke()));
    case ::fuchsia::ui::sketchy::Command::Tag::kRemoveStroke:
      return ApplyRemoveStrokeCmd(std::move(command.remove_stroke()));
    case ::fuchsia::ui::sketchy::Command::Tag::kBeginStroke:
      return ApplyBeginStrokeCmd(std::move(command.begin_stroke()));
    case ::fuchsia::ui::sketchy::Command::Tag::kExtendStroke:
      return ApplyExtendStrokeCmd(std::move(command.extend_stroke()));
    case ::fuchsia::ui::sketchy::Command::Tag::kFinishStroke:
      return ApplyFinishStrokeCmd(std::move(command.finish_stroke()));
    case ::fuchsia::ui::sketchy::Command::Tag::kClearGroup:
      return ApplyClearGroupCmd(std::move(command.clear_group()));
    case ::fuchsia::ui::sketchy::Command::Tag::kScenicImportResource:
      return ApplyScenicImportResourceCmd(
          std::move(command.scenic_import_resource()));
    case ::fuchsia::ui::sketchy::Command::Tag::kScenicAddChild:
      return ApplyScenicAddChildCmd(std::move(command.scenic_add_child()));
    default:
      FXL_DCHECK(false) << "Unsupported op: "
                        << static_cast<uint32_t>(command.Which());
      return false;
  }
}

bool CanvasImpl::ApplyCreateResourceCmd(
    ::fuchsia::ui::sketchy::CreateResourceCmd create_resource) {
  switch (create_resource.args.Which()) {
    case ::fuchsia::ui::sketchy::ResourceArgs::Tag::kStroke:
      return CreateStroke(create_resource.id, create_resource.args.stroke());
    case ::fuchsia::ui::sketchy::ResourceArgs::Tag::kStrokeGroup:
      return CreateStrokeGroup(create_resource.id,
                               create_resource.args.stroke_group());
    default:
      FXL_DCHECK(false) << "Unsupported resource: "
                        << static_cast<uint32_t>(create_resource.args.Which());
      return false;
  }
}

bool CanvasImpl::CreateStroke(ResourceId id,
                              ::fuchsia::ui::sketchy::Stroke stroke) {
  return resource_map_.AddResource(
      id, fxl::MakeRefCounted<Stroke>(stroke_manager_.stroke_tessellator(),
                                      shared_buffer_pool_.factory()));
}

bool CanvasImpl::CreateStrokeGroup(
    ResourceId id, ::fuchsia::ui::sketchy::StrokeGroup stroke_group) {
  return resource_map_.AddResource(id,
                                   fxl::MakeRefCounted<StrokeGroup>(session_));
}

bool CanvasImpl::ApplyReleaseResourceCmd(
    ::fuchsia::ui::sketchy::ReleaseResourceCmd command) {
  return resource_map_.RemoveResource(command.id);
}

bool CanvasImpl::ApplySetPathCmd(
    ::fuchsia::ui::sketchy::SetStrokePathCmd command) {
  auto stroke = resource_map_.FindResource<Stroke>(command.stroke_id);
  if (!stroke) {
    FXL_LOG(ERROR) << "No Stroke of id " << command.stroke_id << " was found!";
    return false;
  }
  return stroke_manager_.SetStrokePath(
      stroke, std::make_unique<StrokePath>(std::move(command.path)));
}

bool CanvasImpl::ApplyAddStrokeCmd(
    ::fuchsia::ui::sketchy::AddStrokeCmd command) {
  auto stroke = resource_map_.FindResource<Stroke>(command.stroke_id);
  if (!stroke) {
    FXL_LOG(ERROR) << "No Stroke of id " << command.stroke_id << " was found!";
    return false;
  }
  auto group = resource_map_.FindResource<StrokeGroup>(command.group_id);
  if (!group) {
    FXL_LOG(ERROR) << "No StrokeGroup of id " << command.group_id
                   << " was found!";
    return false;
  }
  return stroke_manager_.AddStrokeToGroup(stroke, group);
}

bool CanvasImpl::ApplyRemoveStrokeCmd(
    ::fuchsia::ui::sketchy::RemoveStrokeCmd command) {
  auto stroke = resource_map_.FindResource<Stroke>(command.stroke_id);
  if (!stroke) {
    FXL_LOG(ERROR) << "No Stroke of id " << command.stroke_id << " was found!";
    return false;
  }
  auto group = resource_map_.FindResource<StrokeGroup>(command.group_id);
  if (!group) {
    FXL_LOG(ERROR) << "No StrokeGroup of id " << command.group_id
                   << " was found!";
    return false;
  }
  return stroke_manager_.RemoveStrokeFromGroup(stroke, group);
}

bool CanvasImpl::ApplyBeginStrokeCmd(
    ::fuchsia::ui::sketchy::BeginStrokeCmd command) {
  auto stroke = resource_map_.FindResource<Stroke>(command.stroke_id);
  if (!stroke) {
    FXL_LOG(ERROR) << "No Stroke of id " << command.stroke_id << " was found!";
    return false;
  }
  const auto& pos = command.touch.position;
  return stroke_manager_.BeginStroke(stroke, {pos.x, pos.y});
}

bool CanvasImpl::ApplyExtendStrokeCmd(
    ::fuchsia::ui::sketchy::ExtendStrokeCmd command) {
  auto stroke = resource_map_.FindResource<Stroke>(command.stroke_id);
  if (!stroke) {
    FXL_LOG(ERROR) << "No Stroke of id " << command.stroke_id << " was found!";
    return false;
  }
  std::vector<glm::vec2> pts;
  pts.reserve(command.touches->size());
  for (const auto& touch : *command.touches) {
    pts.push_back({touch.position.x, touch.position.y});
  }
  return stroke_manager_.ExtendStroke(stroke, std::move(pts));
}

bool CanvasImpl::ApplyFinishStrokeCmd(
    ::fuchsia::ui::sketchy::FinishStrokeCmd command) {
  auto stroke = resource_map_.FindResource<Stroke>(command.stroke_id);
  if (!stroke) {
    FXL_LOG(ERROR) << "No Stroke of id " << command.stroke_id << " was found!";
    return false;
  }
  return stroke_manager_.FinishStroke(stroke);
}

bool CanvasImpl::ApplyClearGroupCmd(
    ::fuchsia::ui::sketchy::ClearGroupCmd command) {
  auto group = resource_map_.FindResource<StrokeGroup>(command.group_id);
  if (!group) {
    FXL_LOG(ERROR) << "No Group of id " << command.group_id << " was found!";
    return false;
  }
  return stroke_manager_.ClearGroup(group);
}

bool CanvasImpl::ApplyScenicImportResourceCmd(
    fuchsia::ui::gfx::ImportResourceCmd import_resource) {
  switch (import_resource.spec) {
    case fuchsia::ui::gfx::ImportSpec::NODE:
      return ScenicImportNode(import_resource.id,
                              std::move(import_resource.token));
  }
}

bool CanvasImpl::ScenicImportNode(ResourceId id, zx::eventpair token) {
  // As a client of Scenic, Canvas creates an ImportNode given token.
  auto node = fxl::MakeRefCounted<ImportNode>(session_, std::move(token));
  resource_map_.AddResource(id, std::move(node));
  return true;
}

bool CanvasImpl::ApplyScenicAddChildCmd(
    fuchsia::ui::gfx::AddChildCmd add_child) {
  auto import_node = resource_map_.FindResource<ImportNode>(add_child.node_id);
  auto stroke_group =
      resource_map_.FindResource<StrokeGroup>(add_child.child_id);
  if (!import_node || !stroke_group) {
    return false;
  }
  import_node->AddChild(stroke_group);
  return stroke_manager_.AddNewGroup(stroke_group);
}

}  // namespace sketchy_service
