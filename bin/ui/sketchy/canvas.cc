// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/canvas.h"
#include "garnet/bin/ui/sketchy/resources/import_node.h"
#include "garnet/bin/ui/sketchy/resources/stroke_group.h"
#include "lib/fsl/tasks/message_loop.h"

namespace sketchy_service {

using namespace sketchy;

CanvasImpl::CanvasImpl(scenic_lib::Session* session, escher::Escher* escher)
    : session_(session), buffer_factory_(escher) {}

void CanvasImpl::Init(::fidl::InterfaceHandle<CanvasListener> listener) {
  // TODO(MZ-269): unimplemented.
  FXL_LOG(ERROR) << "Init: unimplemented.";
}

void CanvasImpl::Enqueue(::fidl::Array<OpPtr> ops) {
  // TODO: Use `AddAll()` when ::fidl::Array supports it.
  for (auto& op : ops) {
    ops_.push_back(std::move(op));
  }
}

void CanvasImpl::Present(uint64_t presentation_time,
                         const PresentCallback& callback) {
  for (auto& op : ops_) {
    if (!ApplyOp(op)) {
      fsl::MessageLoop::GetCurrent()->QuitNow();
    }
  }
  ops_.reset();
  session_->Present(presentation_time, callback);
}

bool CanvasImpl::ApplyOp(const OpPtr& op) {
  switch (op->which()) {
    case Op::Tag::CREATE_RESOURCE:
      return ApplyCreateResourceOp(op->get_create_resource());
    case Op::Tag::RELEASE_RESOURCE:
      return ApplyReleaseResourceOp(op->get_release_resource());
    case Op::Tag::ADD_STROKE:
      return ApplyAddStrokeOp(op->get_add_stroke());
    case Op::Tag::REMOVE_STROKE:
      return ApplyRemoveStrokeOp(op->get_remove_stroke());
    case Op::Tag::SCENIC_IMPORT_RESOURCE:
      return ApplyScenicImportResourceOp(op->get_scenic_import_resource());
    case Op::Tag::SCENIC_ADD_CHILD:
      return ApplyScenicAddChildOp(op->get_scenic_add_child());
    default:
      FXL_DCHECK(false) << "Unsupported op: "
                        << static_cast<uint32_t>(op->which());
      return false;
  }
}

bool CanvasImpl::ApplyCreateResourceOp(
    const CreateResourceOpPtr& create_resource) {
  switch (create_resource->args->which()) {
    case ResourceArgs::Tag::STROKE:
      return CreateStroke(create_resource->id,
                          create_resource->args->get_stroke());
    case ResourceArgs::Tag::STROKE_GROUP:
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
  // TODO(MZ-269): unimplemented.
  FXL_LOG(ERROR) << "CreateStroke: unimplemented.";
  // This dummy true is for the client demo.
  return true;
}

bool CanvasImpl::CreateStrokeGroup(
    ResourceId id,
    const sketchy::StrokeGroupPtr& stroke_group) {
  return resource_map_.AddResource(
      id, fxl::MakeRefCounted<StrokeGroup>(session_, &buffer_factory_));
}

bool CanvasImpl::ApplyReleaseResourceOp(const ReleaseResourceOpPtr& op) {
  return resource_map_.RemoveResource(op->id);
}

bool CanvasImpl::ApplyAddStrokeOp(const AddStrokeOpPtr& op) {
  // TODO(MZ-269): unimplemented.
  FXL_LOG(ERROR) << "ApplyAddStrokeOp: unimplemented.";
  // This dummy true is for the client demo.
  return true;
}

bool CanvasImpl::ApplyRemoveStrokeOp(const RemoveStrokeOpPtr& op) {
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

// Delegates ImportNodeOp to Scenic.
// @param id Id of the node that the client exports.
// @param token Token of the node that the client exports.
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
