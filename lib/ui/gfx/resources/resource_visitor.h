// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_RESOURCE_VISITOR_H_
#define GARNET_LIB_UI_GFX_RESOURCES_RESOURCE_VISITOR_H_

namespace scenic {
namespace gfx {

class Import;
class GpuMemory;
class HostMemory;
class Image;
class ImagePipe;
class Buffer;
class View;
class ViewHolder;
class EntityNode;
class OpacityNode;
class ShapeNode;
class Scene;
class CircleShape;
class RectangleShape;
class RoundedRectangleShape;
class MeshShape;
class Material;
class DisplayCompositor;
class LayerStack;
class Layer;
class Camera;
class Renderer;
class Scene;
class Light;
class AmbientLight;
class DirectionalLight;

class ResourceVisitor {
 public:
  // Memory resources.
  virtual void Visit(GpuMemory* r) = 0;
  virtual void Visit(HostMemory* r) = 0;
  virtual void Visit(Image* r) = 0;
  virtual void Visit(ImagePipe* r) = 0;
  virtual void Visit(Buffer* r) = 0;

  // Views.
  virtual void Visit(View* r) = 0;
  virtual void Visit(ViewHolder* r) = 0;

  // Nodes.
  virtual void Visit(EntityNode* r) = 0;
  virtual void Visit(OpacityNode* r) = 0;
  virtual void Visit(ShapeNode* r) = 0;

  // Shapes.
  virtual void Visit(CircleShape* r) = 0;
  virtual void Visit(RectangleShape* r) = 0;
  virtual void Visit(RoundedRectangleShape* r) = 0;
  virtual void Visit(MeshShape* r) = 0;

  // Materials.
  virtual void Visit(Material* r) = 0;

  // Layers.
  virtual void Visit(DisplayCompositor* r) = 0;
  virtual void Visit(LayerStack* r) = 0;
  virtual void Visit(Layer* r) = 0;

  // Scene, camera, lighting.
  virtual void Visit(Scene* r) = 0;
  virtual void Visit(Camera* r) = 0;
  virtual void Visit(Renderer* r) = 0;
  virtual void Visit(Light* r) = 0;
  virtual void Visit(AmbientLight* r) = 0;
  virtual void Visit(DirectionalLight* r) = 0;

  // Imported resources.
  virtual void Visit(Import* r) = 0;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_RESOURCE_VISITOR_H_
