// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace mozart {
namespace scene {

class GpuMemory;
class HostMemory;
class Image;
class EntityNode;
class Node;
class ShapeNode;
class CircleShape;
class Shape;
class Scene;
class Material;

class ResourceVisitor {
 public:
  virtual void Visit(GpuMemory* r) = 0;
  virtual void Visit(HostMemory* r) = 0;
  virtual void Visit(Image* r) = 0;
  virtual void Visit(EntityNode* r) = 0;
  virtual void Visit(Node* r) = 0;
  virtual void Visit(ShapeNode* r) = 0;
  virtual void Visit(CircleShape* r) = 0;
  virtual void Visit(Shape* r) = 0;
  virtual void Visit(Scene* r) = 0;
  virtual void Visit(Material* r) = 0;
};

}  // namespace scene
}  // namespace mozart
