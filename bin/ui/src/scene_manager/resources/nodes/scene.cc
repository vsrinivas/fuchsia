// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/resources/nodes/scene.h"

namespace mozart {
namespace scene {

const ResourceTypeInfo Scene::kTypeInfo = {
    ResourceType::kNode | ResourceType::kScene, "Scene"};

Scene::Scene(Session* session, ResourceId node_id)
    : Node(session, node_id, Scene::kTypeInfo) {}

}  // namespace scene
}  // namespace mozart
