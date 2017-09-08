// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/resources/resource_visitor.h"

#include "apps/mozart/src/scene_manager/resources/buffer.h"
#include "apps/mozart/src/scene_manager/resources/camera.h"
#include "apps/mozart/src/scene_manager/resources/compositor/compositor.h"
#include "apps/mozart/src/scene_manager/resources/compositor/display_compositor.h"
#include "apps/mozart/src/scene_manager/resources/compositor/layer.h"
#include "apps/mozart/src/scene_manager/resources/compositor/layer_stack.h"
#include "apps/mozart/src/scene_manager/resources/gpu_memory.h"
#include "apps/mozart/src/scene_manager/resources/host_memory.h"
#include "apps/mozart/src/scene_manager/resources/image.h"
#include "apps/mozart/src/scene_manager/resources/image_pipe.h"
#include "apps/mozart/src/scene_manager/resources/import.h"
#include "apps/mozart/src/scene_manager/resources/lights/directional_light.h"
#include "apps/mozart/src/scene_manager/resources/material.h"
#include "apps/mozart/src/scene_manager/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene_manager/resources/nodes/node.h"
#include "apps/mozart/src/scene_manager/resources/nodes/scene.h"
#include "apps/mozart/src/scene_manager/resources/nodes/shape_node.h"
#include "apps/mozart/src/scene_manager/resources/renderers/renderer.h"
#include "apps/mozart/src/scene_manager/resources/shapes/circle_shape.h"
#include "apps/mozart/src/scene_manager/resources/shapes/mesh_shape.h"
#include "apps/mozart/src/scene_manager/resources/shapes/rectangle_shape.h"
#include "apps/mozart/src/scene_manager/resources/shapes/rounded_rectangle_shape.h"
#include "apps/mozart/src/scene_manager/resources/shapes/shape.h"

namespace scene_manager {

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

void Buffer::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void EntityNode::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void ShapeNode::Accept(ResourceVisitor* visitor) {
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

void MeshShape::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void Material::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void DisplayCompositor::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void LayerStack::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void Layer::Accept(ResourceVisitor* visitor) {
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

}  // namespace scene_manager
