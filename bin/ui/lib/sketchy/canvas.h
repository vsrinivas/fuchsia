// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "application/lib/app/application_context.h"
#include "apps/mozart/lib/scene/client/resources.h"
#include "apps/mozart/lib/scene/client/session.h"
#include "apps/mozart/lib/sketchy/resources.h"
#include "apps/mozart/services/fun/sketchy/canvas.fidl.h"

namespace sketchy_lib {

// Convenient C++ wrapper for sketchy::Canvas service.
class Canvas final {
 public:
  friend class ResourceManager;

  Canvas(app::ApplicationContext* context);
  Canvas(sketchy::CanvasPtr canvas);
  ResourceManager* resources() { return resources_.get(); }

  // Exports the specified node from its Session, and passes
  // the token to the Sketchy service, which imports it.  The
  // imported node can then be referenced by the returned ID
  // (see, for example, AddStrokeGroupToNode()).
  ResourceId ImportNode(mozart::client::EntityNode* node);

  void AddStrokeToNode(
      ResourceId stroke_id, ResourceId node_id);
  void AddStrokeToGroup(
      ResourceId stroke_id, ResourceId group_id);
  void AddStrokeGroupToNode(
      ResourceId group_id, ResourceId node_id);
  void Present(uint64_t time);

 private:
  void AddChildToNode(ResourceId child_id, ResourceId node_id);

  sketchy::CanvasPtr canvas_;
  std::unique_ptr<ResourceManager> resources_;
  fidl::Array<sketchy::OpPtr> ops_;
};

}  // namespace sketchy_lib
