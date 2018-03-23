// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_CANVAS_H_
#define GARNET_BIN_UI_SKETCHY_CANVAS_H_

#include <unordered_map>

#include <fuchsia/cpp/sketchy.h>

#include "garnet/bin/ui/sketchy/buffer/shared_buffer_pool.h"
#include "garnet/bin/ui/sketchy/resources/resource_map.h"
#include "garnet/bin/ui/sketchy/resources/stroke_group.h"
#include "garnet/bin/ui/sketchy/resources/types.h"
#include "garnet/bin/ui/sketchy/stroke/stroke_manager.h"
#include "lib/escher/escher.h"
#include "lib/escher/vk/buffer_factory.h"
#include "lib/ui/scenic/client/session.h"

namespace sketchy_service {

class CanvasImpl final : public sketchy::Canvas {
 public:
  CanvasImpl(scenic_lib::Session* session, escher::Escher* escher);

  // |sketchy::Canvas|
  void Init(::fidl::InterfaceHandle<sketchy::CanvasListener> listener) override;
  void Enqueue(::fidl::VectorPtr<sketchy::Command> commands) override;
  void Present(uint64_t presentation_time, PresentCallback callback) override;

 private:
  bool ApplyCommand(sketchy::Command command);
  void RequestScenicPresent(uint64_t presentation_time);

  bool ApplyCreateResourceCommand(sketchy::CreateResourceCommand command);
  bool ApplyReleaseResourceCommand(sketchy::ReleaseResourceCommand command);
  bool CreateStroke(ResourceId id, sketchy::Stroke stroke);
  bool CreateStrokeGroup(ResourceId id, sketchy::StrokeGroup stroke_group);

  bool ApplySetPathCommand(sketchy::SetStrokePathCommand command);
  bool ApplyAddStrokeCommand(sketchy::AddStrokeCommand command);
  bool ApplyRemoveStrokeCommand(sketchy::RemoveStrokeCommand command);

  bool ApplyBeginStrokeCommand(sketchy::BeginStrokeCommand command);
  bool ApplyExtendStrokeCommand(sketchy::ExtendStrokeCommand command);
  bool ApplyFinishStrokeCommand(sketchy::FinishStrokeCommand command);

  bool ApplyClearGroupCommand(sketchy::ClearGroupCommand command);

  bool ApplyScenicImportResourceCommand(
      gfx::ImportResourceCommand import_resource);

  // Imports an exported ScenicNode that can be used as an
  // attachment point for a StrokeGroup.
  //
  // |id| ID that can be used by the Canvas client to refer to
  //     the imported node.
  // |token| Token that the Sketchy service will pass along
  //     to the SceneManager to import the node.
  bool ScenicImportNode(ResourceId id, zx::eventpair token);

  bool ApplyScenicAddChildCommand(gfx::AddChildCommand add_child);

  scenic_lib::Session* const session_;
  SharedBufferPool shared_buffer_pool_;

  ::fidl::VectorPtr<sketchy::Command> commands_;
  ResourceMap resource_map_;
  bool is_scenic_present_requested_ = false;
  std::vector<scenic_lib::Session::PresentCallback> callbacks_;

  StrokeManager stroke_manager_;
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_CANVAS_H_
