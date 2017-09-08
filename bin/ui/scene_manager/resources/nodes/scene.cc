// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/resources/nodes/scene.h"

namespace scene_manager {

const ResourceTypeInfo Scene::kTypeInfo = {
    ResourceType::kNode | ResourceType::kScene, "Scene"};

Scene::Scene(Session* session, scenic::ResourceId node_id)
    : Node(session, node_id, Scene::kTypeInfo) {}

bool Scene::Detach() {
  return Resource::Detach();
}

}  // namespace scene_manager
