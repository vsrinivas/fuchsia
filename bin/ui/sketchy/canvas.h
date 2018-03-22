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
#include "lib/ui/scenic/client/session.h"
#include "lib/ui/sketchy/fidl/canvas.fidl.h"

namespace sketchy_service {

class CanvasImpl final : public sketchy::Canvas {
 public:
  CanvasImpl(scenic_lib::Session* session, escher::Escher* escher);

  // |sketchy::Canvas|
  void Init(::fidl::InterfaceHandle<sketchy::CanvasListener> listener) override;
  void Enqueue(::fidl::VectorPtr<sketchy::CommandPtr> commands) override;
  void Present(uint64_t presentation_time,
               const PresentCallback& callback) override;

 private:
  bool ApplyCommand(const sketchy::CommandPtr& command);
  void RequestScenicPresent(uint64_t presentation_time);

  bool ApplyCreateResourceCommand(
      const sketchy::CreateResourceCommandPtr& command);
  bool ApplyReleaseResourceCommand(
      const sketchy::ReleaseResourceCommandPtr& command);
  bool CreateStroke(ResourceId id, const sketchy::StrokePtr& stroke);
  bool CreateStrokeGroup(ResourceId id,
                         const sketchy::StrokeGroupPtr& stroke_group);

  bool ApplySetPathCommand(const sketchy::SetStrokePathCommandPtr& command);
  bool ApplyAddStrokeCommand(const sketchy::AddStrokeCommandPtr& command);
  bool ApplyRemoveStrokeCommand(const sketchy::RemoveStrokeCommandPtr& command);

  bool ApplyBeginStrokeCommand(const sketchy::BeginStrokeCommandPtr& command);
  bool ApplyExtendStrokeCommand(const sketchy::ExtendStrokeCommandPtr& command);
  bool ApplyFinishStrokeCommand(const sketchy::FinishStrokeCommandPtr& command);

  bool ApplyClearGroupCommand(const sketchy::ClearGroupCommandPtr& command);

  bool ApplyScenicImportResourceCommand(
      const gfx::ImportResourceCommandPtr& import_resource);

  // Imports an exported ScenicNode that can be used as an
  // attachment point for a StrokeGroup.
  //
  // |id| ID that can be used by the Canvas client to refer to
  //     the imported node.
  // |token| Token that the Sketchy service will pass along
  //     to the SceneManager to import the node.
  bool ScenicImportNode(ResourceId id, zx::eventpair token);

  bool ApplyScenicAddChildCommand(const gfx::AddChildCommandPtr& add_child);

  scenic_lib::Session* const session_;
  SharedBufferPool shared_buffer_pool_;

  ::fidl::VectorPtr<sketchy::CommandPtr> commands_;
  ResourceMap resource_map_;
  bool is_scenic_present_requested_ = false;
  std::vector<scenic_lib::Session::PresentCallback> callbacks_;

  StrokeManager stroke_manager_;
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_CANVAS_H_
