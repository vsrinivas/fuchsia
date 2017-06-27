// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/resources/resource_visitor.h"

#include "apps/mozart/src/scene/renderer/display_renderer.h"
#include "apps/mozart/src/scene/renderer/renderer.h"
#include "apps/mozart/src/scene/resources/camera.h"
#include "apps/mozart/src/scene/resources/gpu_memory.h"
#include "apps/mozart/src/scene/resources/host_memory.h"
#include "apps/mozart/src/scene/resources/image.h"
#include "apps/mozart/src/scene/resources/image_pipe.h"
#include "apps/mozart/src/scene/resources/import.h"
#include "apps/mozart/src/scene/resources/lights/directional_light.h"
#include "apps/mozart/src/scene/resources/material.h"
#include "apps/mozart/src/scene/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene/resources/nodes/node.h"
#include "apps/mozart/src/scene/resources/nodes/scene.h"
#include "apps/mozart/src/scene/resources/nodes/shape_node.h"
#include "apps/mozart/src/scene/resources/nodes/tag_node.h"
#include "apps/mozart/src/scene/resources/shapes/circle_shape.h"
#include "apps/mozart/src/scene/resources/shapes/rectangle_shape.h"
#include "apps/mozart/src/scene/resources/shapes/rounded_rectangle_shape.h"
#include "apps/mozart/src/scene/resources/shapes/shape.h"

namespace mozart {
namespace scene {

void GpuMemory::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void HostMemory::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void Image::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void ImagePipe::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void EntityNode::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void ShapeNode::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void TagNode::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void CircleShape::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void RectangleShape::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void RoundedRectangleShape::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void Material::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void Scene::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void Camera::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void Renderer::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void DirectionalLight::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void Import::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

}  // namespace scene
}  // namespace mozart
