// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/resource_visitor.h"

#include "garnet/lib/ui/gfx/resources/buffer.h"
#include "garnet/lib/ui/gfx/resources/camera.h"
#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/display_compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/gfx/resources/gpu_image.h"
#include "garnet/lib/ui/gfx/resources/gpu_memory.h"
#include "garnet/lib/ui/gfx/resources/host_image.h"
#include "garnet/lib/ui/gfx/resources/host_memory.h"
#include "garnet/lib/ui/gfx/resources/image_pipe.h"
#include "garnet/lib/ui/gfx/resources/import.h"
#include "garnet/lib/ui/gfx/resources/lights/ambient_light.h"
#include "garnet/lib/ui/gfx/resources/lights/directional_light.h"
#include "garnet/lib/ui/gfx/resources/lights/light.h"
#include "garnet/lib/ui/gfx/resources/material.h"
#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/node.h"
#include "garnet/lib/ui/gfx/resources/nodes/opacity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/scene.h"
#include "garnet/lib/ui/gfx/resources/nodes/shape_node.h"
#include "garnet/lib/ui/gfx/resources/renderers/renderer.h"
#include "garnet/lib/ui/gfx/resources/shapes/circle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/mesh_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/rectangle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/rounded_rectangle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/shape.h"
#include "garnet/lib/ui/gfx/resources/view.h"
#include "garnet/lib/ui/gfx/resources/view_holder.h"

namespace scenic {
namespace gfx {

void GpuMemory::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void HostMemory::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void GpuImage::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void HostImage::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void ImagePipe::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void View::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void ViewHolder::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void Buffer::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void EntityNode::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void OpacityNode::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void ShapeNode::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void CircleShape::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void RectangleShape::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void RoundedRectangleShape::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void MeshShape::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void Material::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void DisplayCompositor::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void LayerStack::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void Layer::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void Scene::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void Camera::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void Renderer::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void Light::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void AmbientLight::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void DirectionalLight::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void Import::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

}  // namespace gfx
}  // namespace scenic
