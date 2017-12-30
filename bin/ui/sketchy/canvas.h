// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_CANVAS_H_
#define GARNET_BIN_UI_SKETCHY_CANVAS_H_

#include <unordered_map>
#include "garnet/bin/ui/sketchy/buffer/shared_buffer_pool.h"
#include "garnet/bin/ui/sketchy/resources/resource_map.h"
#include "garnet/bin/ui/sketchy/resources/stroke_group.h"
#include "garnet/bin/ui/sketchy/resources/types.h"
#include "garnet/bin/ui/sketchy/stroke/stroke_manager.h"
#include "lib/escher/escher.h"
#include "lib/escher/vk/buffer_factory.h"
#include "lib/ui/fun/sketchy/fidl/canvas.fidl.h"
#include "lib/ui/scenic/client/session.h"

namespace sketchy_service {

class CanvasImpl final : public sketchy::Canvas {
 public:
  CanvasImpl(scenic_lib::Session* session, escher::Escher* escher);

  // |sketchy::Canvas|
  void Init(::fidl::InterfaceHandle<sketchy::CanvasListener> listener) override;
  void Enqueue(::fidl::Array<sketchy::OpPtr> ops) override;
  void Present(uint64_t presentation_time,
               const PresentCallback& callback) override;

 private:
  bool ApplyOp(const sketchy::OpPtr& op);
  void RequestScenicPresent(uint64_t presentation_time);

  bool ApplyCreateResourceOp(const sketchy::CreateResourceOpPtr& op);
  bool ApplyReleaseResourceOp(const sketchy::ReleaseResourceOpPtr& op);
  bool CreateStroke(ResourceId id, const sketchy::StrokePtr& stroke);
  bool CreateStrokeGroup(ResourceId id,
                         const sketchy::StrokeGroupPtr& stroke_group);

  bool ApplySetPathOp(const sketchy::SetStrokePathOpPtr& op);
  bool ApplyAddStrokeOp(const sketchy::AddStrokeOpPtr& op);
  bool ApplyRemoveStrokeOp(const sketchy::RemoveStrokeOpPtr& op);

  bool ApplyBeginStrokeOp(const sketchy::BeginStrokeOpPtr& op);
  bool ApplyExtendStrokeOp(const sketchy::ExtendStrokeOpPtr& op);
  bool ApplyFinishStrokeOp(const sketchy::FinishStrokeOpPtr& op);

  bool ApplyClearGroupOp(const sketchy::ClearGroupOpPtr& op);

  bool ApplyScenicImportResourceOp(
      const scenic::ImportResourceOpPtr& import_resource);

  // Imports an exported ScenicNode that can be used as an
  // attachment point for a StrokeGroup.
  //
  // |id| ID that can be used by the Canvas client to refer to
  //     the imported node.
  // |token| Token that the Sketchy service will pass along
  //     to the SceneManager to import the node.
  bool ScenicImportNode(ResourceId id, zx::eventpair token);

  bool ApplyScenicAddChildOp(const scenic::AddChildOpPtr& add_child);

  scenic_lib::Session* const session_;
  SharedBufferPool shared_buffer_pool_;

  ::fidl::Array<sketchy::OpPtr> ops_;
  ResourceMap resource_map_;
  bool is_scenic_present_requested_ = false;
  std::vector<scenic_lib::Session::PresentCallback> callbacks_;

  StrokeManager stroke_manager_;
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_CANVAS_H_
