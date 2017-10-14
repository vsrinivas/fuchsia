// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "garnet/bin/ui/sketchy/resources/resource_map.h"
#include "garnet/bin/ui/sketchy/resources/stroke_group.h"
#include "garnet/bin/ui/sketchy/resources/types.h"
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

  bool ApplyCreateResourceOp(const sketchy::CreateResourceOpPtr& op);
  bool ApplyReleaseResourceOp(const sketchy::ReleaseResourceOpPtr& op);
  bool CreateStroke(ResourceId id, const sketchy::StrokePtr& stroke);
  bool CreateStrokeGroup(ResourceId id,
                         const sketchy::StrokeGroupPtr& stroke_group);

  bool ApplySetPathOp(const sketchy::SetStrokePathOpPtr& op);
  bool ApplyAddStrokeOp(const sketchy::AddStrokeOpPtr& op);
  bool ApplyRemoveStrokeOp(const sketchy::RemoveStrokeOpPtr& op);

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
  escher::Escher* const escher_;

  // TODO: use more sophisticated factory that suballocates from larger GPU
  // memory allocations, and recycles buffers when they are no longer used.
  escher::BufferFactory buffer_factory_;

  ::fidl::Array<sketchy::OpPtr> ops_;
  ResourceMap resource_map_;
  std::set<StrokeGroupPtr> dirty_stroke_groups_;
};

}  // namespace sketchy_service
