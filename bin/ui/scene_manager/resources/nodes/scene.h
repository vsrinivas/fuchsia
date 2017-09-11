// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/resources/nodes/node.h"
#include "lib/fxl/macros.h"

namespace scene_manager {

class Scene;
using ScenePtr = fxl::RefPtr<Scene>;

class Scene final : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Scene(Session* session, scenic::ResourceId node_id);

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

  bool Detach() override;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Scene);
};

}  // namespace scene_manager
