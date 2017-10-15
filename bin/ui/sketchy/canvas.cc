// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/canvas.h"
#include "garnet/bin/ui/sketchy/escher_utils.h"
#include "garnet/bin/ui/sketchy/resources/import_node.h"
#include "garnet/bin/ui/sketchy/resources/stroke.h"
#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/fsl/tasks/message_loop.h"

namespace sketchy_service {

CanvasImpl::CanvasImpl(scenic_lib::Session* session, escher::Escher* escher)
    : session_(session), escher_(escher), buffer_factory_(escher) {}

void CanvasImpl::Init(fidl::InterfaceHandle<sketchy::CanvasListener> listener) {
  // TODO(MZ-269): unimplemented.
  FXL_LOG(ERROR) << "Init: unimplemented.";
}

void CanvasImpl::Enqueue(fidl::Array<sketchy::OpPtr> ops) {
  // TODO: Use `AddAll()` when fidl::Array supports it.
  for (auto& op : ops) {
    ops_.push_back(std::move(op));
  }
}

void CanvasImpl::Present(uint64_t presentation_time,
                         const PresentCallback& callback) {
  // TODO(MZ-269): Present() should behave the same way as Scenic. Specifically,
  // Ops shouldn't be applied immediately. Instead a frame-request should be
  // triggered and the Ops enqueue; when the corresponding frame is processed
  // all Ops that are scheduled for the current frame's presentation time are
  // applied.
  for (auto& op : ops_) {
    if (!ApplyOp(op)) {
      fsl::MessageLoop::GetCurrent()->QuitNow();
    }
  }
  ops_.reset();

  auto command = escher_->command_buffer_pool()->GetCommandBuffer();
  while (!dirty_stroke_groups_.empty()) {
    const auto& stroke_group = *dirty_stroke_groups_.begin();
    dirty_stroke_groups_.erase(stroke_group);
    stroke_group->ApplyChanges(command, &buffer_factory_);
  }

  auto pair = NewSemaphoreEventPair(escher_);
  command->AddSignalSemaphore(std::move(pair.first));
  session_->EnqueueAcquireFence(std::move(pair.second));

  command->Submit(escher_->device()->vk_main_queue(), {});
  callbacks_.push_back(std::move(callback));
  RequestScenicPresent(presentation_time);
}

void CanvasImpl::RequestScenicPresent(uint64_t presentation_time) {
  if (is_scenic_present_requested_) {
    return;
  }
  is_scenic_present_requested_ = true;

  auto session_callback = [this, callbacks = std::move(callbacks_)]
      (scenic::PresentationInfoPtr info) {
    FXL_DCHECK(is_scenic_present_requested_);
    is_scenic_present_requested_ = false;
    for (auto& callback : callbacks) {
      auto _info = scenic::PresentationInfo::New();
      _info->presentation_time = _info->presentation_time;
      _info->presentation_interval = _info->presentation_interval;
      callback(std::move(_info));
    }
    RequestScenicPresent(info->presentation_time + info->presentation_interval);
  };
  callbacks_.clear();
  session_->Present(presentation_time, std::move(session_callback));
}

bool CanvasImpl::ApplyOp(const sketchy::OpPtr& op) {
  switch (op->which()) {
    case sketchy::Op::Tag::CREATE_RESOURCE:
      return ApplyCreateResourceOp(op->get_create_resource());
    case sketchy::Op::Tag::RELEASE_RESOURCE:
      return ApplyReleaseResourceOp(op->get_release_resource());
    case sketchy::Op::Tag::SET_PATH:
      return ApplySetPathOp(op->get_set_path());
    case sketchy::Op::Tag::ADD_STROKE:
      return ApplyAddStrokeOp(op->get_add_stroke());
    case sketchy::Op::Tag::REMOVE_STROKE:
      return ApplyRemoveStrokeOp(op->get_remove_stroke());
    case sketchy::Op::Tag::SCENIC_IMPORT_RESOURCE:
      return ApplyScenicImportResourceOp(op->get_scenic_import_resource());
    case sketchy::Op::Tag::SCENIC_ADD_CHILD:
      return ApplyScenicAddChildOp(op->get_scenic_add_child());
    default:
      FXL_DCHECK(false) << "Unsupported op: "
                        << static_cast<uint32_t>(op->which());
      return false;
  }
}

bool CanvasImpl::ApplyCreateResourceOp(
    const sketchy::CreateResourceOpPtr& create_resource) {
  switch (create_resource->args->which()) {
    case sketchy::ResourceArgs::Tag::STROKE:
      return CreateStroke(create_resource->id,
                          create_resource->args->get_stroke());
    case sketchy::ResourceArgs::Tag::STROKE_GROUP:
      return CreateStrokeGroup(create_resource->id,
                               create_resource->args->get_stroke_group());
    default:
      FXL_DCHECK(false) << "Unsupported resource: "
                        << static_cast<uint32_t>(
                               create_resource->args->which());
      return false;
  }
}

bool CanvasImpl::CreateStroke(ResourceId id, const sketchy::StrokePtr& stroke) {
  return resource_map_.AddResource(id, fxl::MakeRefCounted<Stroke>(escher_));
}

bool CanvasImpl::CreateStrokeGroup(
    ResourceId id,
    const sketchy::StrokeGroupPtr& stroke_group) {
  return resource_map_.AddResource(
      id, fxl::MakeRefCounted<StrokeGroup>(session_, &buffer_factory_));
}

bool CanvasImpl::ApplyReleaseResourceOp(
    const sketchy::ReleaseResourceOpPtr& op) {
  return resource_map_.RemoveResource(op->id);
}

bool CanvasImpl::ApplySetPathOp(const sketchy::SetStrokePathOpPtr& op) {
  auto stroke = resource_map_.FindResource<Stroke>(op->stroke_id);
  return stroke->SetPath(std::move(op->path));
}

bool CanvasImpl::ApplyAddStrokeOp(const sketchy::AddStrokeOpPtr& op) {
  auto stroke = resource_map_.FindResource<Stroke>(op->stroke_id);
  if (!stroke) {
    FXL_LOG(ERROR) << "No Stroke of id " << op->stroke_id << " was found!";
    return false;
  }
  auto group = resource_map_.FindResource<StrokeGroup>(op->group_id);
  if (!group) {
    FXL_LOG(ERROR) << "No StrokeGroup of id " << op->group_id << " was found!";
    return false;
  }
  dirty_stroke_groups_.insert(group);
  return group->AddStroke(std::move(stroke));
}

bool CanvasImpl::ApplyRemoveStrokeOp(const sketchy::RemoveStrokeOpPtr& op) {
  // TODO(MZ-269): unimplemented.
  FXL_LOG(ERROR) << "ApplyRemoveStrokeOp: unimplemented.";
  return false;
}

bool CanvasImpl::ApplyScenicImportResourceOp(
    const scenic::ImportResourceOpPtr& import_resource) {
  switch (import_resource->spec) {
    case scenic::ImportSpec::NODE:
      return ScenicImportNode(import_resource->id,
                              std::move(import_resource->token));
  }
}

bool CanvasImpl::ScenicImportNode(ResourceId id, zx::eventpair token) {
  FXL_LOG(INFO) << "CanvasImpl::ScenicImportNode()";
  // As a client of Scenic, Canvas creates an ImportNode given token.
  auto node = fxl::MakeRefCounted<ImportNode>(session_, std::move(token));
  resource_map_.AddResource(id, std::move(node));
  return true;
}

bool CanvasImpl::ApplyScenicAddChildOp(const scenic::AddChildOpPtr& add_child) {
  auto import_node = resource_map_.FindResource<ImportNode>(add_child->node_id);
  auto stroke_group =
      resource_map_.FindResource<StrokeGroup>(add_child->child_id);
  if (!import_node || !stroke_group) {
    return false;
  }

  import_node->AddChild(stroke_group);
  return true;
}

}  // namespace sketchy_service
