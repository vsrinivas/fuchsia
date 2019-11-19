// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/resource_visitor.h"

#include "src/ui/scenic/lib/gfx/resources/buffer.h"
#include "src/ui/scenic/lib/gfx/resources/camera.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/display_compositor.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/gfx/resources/gpu_image.h"
#include "src/ui/scenic/lib/gfx/resources/host_image.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe_base.h"
#include "src/ui/scenic/lib/gfx/resources/lights/ambient_light.h"
#include "src/ui/scenic/lib/gfx/resources/lights/directional_light.h"
#include "src/ui/scenic/lib/gfx/resources/lights/light.h"
#include "src/ui/scenic/lib/gfx/resources/lights/point_light.h"
#include "src/ui/scenic/lib/gfx/resources/material.h"
#include "src/ui/scenic/lib/gfx/resources/memory.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/entity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/opacity_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/scene.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/view_node.h"
#include "src/ui/scenic/lib/gfx/resources/renderers/renderer.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/circle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/mesh_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/rectangle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/rounded_rectangle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/shape.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"

namespace scenic_impl {
namespace gfx {

void Memory::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void GpuImage::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void HostImage::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void ImagePipeBase::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void View::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void ViewNode::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void ViewHolder::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void Buffer::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void EntityNode::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void OpacityNode::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void ShapeNode::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void CircleShape::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void RectangleShape::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void RoundedRectangleShape::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void MeshShape::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void Material::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void Compositor::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void DisplayCompositor::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void LayerStack::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void Layer::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void Scene::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void Camera::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void Renderer::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void Light::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void AmbientLight::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void DirectionalLight::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

void PointLight::Accept(ResourceVisitor* visitor) { visitor->Visit(this); }

}  // namespace gfx
}  // namespace scenic_impl
