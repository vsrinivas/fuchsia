// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_CANVAS_H_
#define GARNET_BIN_UI_SKETCHY_CANVAS_H_

#include <unordered_map>

#include <fuchsia/ui/sketchy/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/ui/sketchy/buffer/shared_buffer_pool.h"
#include "garnet/bin/ui/sketchy/resources/resource_map.h"
#include "garnet/bin/ui/sketchy/resources/stroke_group.h"
#include "garnet/bin/ui/sketchy/resources/types.h"
#include "garnet/bin/ui/sketchy/stroke/stroke_manager.h"
#include "lib/escher/escher.h"
#include "lib/escher/vk/buffer_factory.h"
#include "lib/ui/scenic/client/session.h"

namespace sketchy_service {

class CanvasImpl final : public ::fuchsia::ui::sketchy::Canvas {
 public:
  CanvasImpl(async::Loop* loop, scenic_lib::Session* session,
             escher::Escher* escher);

  // |::fuchsia::ui::sketchy::Canvas|
  void Init(::fidl::InterfaceHandle<::fuchsia::ui::sketchy::CanvasListener>
                listener) override;
  void Enqueue(
      ::fidl::VectorPtr<::fuchsia::ui::sketchy::Command> commands) override;
  void Present(uint64_t presentation_time, PresentCallback callback) override;

 private:
  bool ApplyCommand(::fuchsia::ui::sketchy::Command command);
  void RequestScenicPresent(uint64_t presentation_time);

  bool ApplyCreateResourceCommand(
      ::fuchsia::ui::sketchy::CreateResourceCommand command);
  bool ApplyReleaseResourceCommand(
      ::fuchsia::ui::sketchy::ReleaseResourceCommand command);
  bool CreateStroke(ResourceId id, ::fuchsia::ui::sketchy::Stroke stroke);
  bool CreateStrokeGroup(ResourceId id,
                         ::fuchsia::ui::sketchy::StrokeGroup stroke_group);

  bool ApplySetPathCommand(
      ::fuchsia::ui::sketchy::SetStrokePathCommand command);
  bool ApplyAddStrokeCommand(::fuchsia::ui::sketchy::AddStrokeCommand command);
  bool ApplyRemoveStrokeCommand(
      ::fuchsia::ui::sketchy::RemoveStrokeCommand command);

  bool ApplyBeginStrokeCommand(
      ::fuchsia::ui::sketchy::BeginStrokeCommand command);
  bool ApplyExtendStrokeCommand(
      ::fuchsia::ui::sketchy::ExtendStrokeCommand command);
  bool ApplyFinishStrokeCommand(
      ::fuchsia::ui::sketchy::FinishStrokeCommand command);

  bool ApplyClearGroupCommand(
      ::fuchsia::ui::sketchy::ClearGroupCommand command);

  bool ApplyScenicImportResourceCommand(
      fuchsia::ui::gfx::ImportResourceCommand import_resource);

  // Imports an exported ScenicNode that can be used as an
  // attachment point for a StrokeGroup.
  //
  // |id| ID that can be used by the Canvas client to refer to
  //     the imported node.
  // |token| Token that the Sketchy service will pass along
  //     to the SceneManager to import the node.
  bool ScenicImportNode(ResourceId id, zx::eventpair token);

  bool ApplyScenicAddChildCommand(fuchsia::ui::gfx::AddChildCommand add_child);

  async::Loop* const loop_;
  scenic_lib::Session* const session_;
  SharedBufferPool shared_buffer_pool_;

  ::fidl::VectorPtr<::fuchsia::ui::sketchy::Command> commands_;
  ResourceMap resource_map_;
  bool is_scenic_present_requested_ = false;
  std::vector<scenic_lib::Session::PresentCallback> callbacks_;

  StrokeManager stroke_manager_;
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_CANVAS_H_
