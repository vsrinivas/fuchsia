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
class TagNode;
class Scene;
class CircleShape;
class RectangleShape;
class RoundedRectangleShape;
class Material;
class Import;

class ResourceVisitor {
 public:
  virtual void Visit(GpuMemory* r) = 0;
  virtual void Visit(HostMemory* r) = 0;
  virtual void Visit(Image* r) = 0;
  virtual void Visit(EntityNode* r) = 0;
  virtual void Visit(ShapeNode* r) = 0;
  virtual void Visit(TagNode* r) = 0;
  virtual void Visit(Scene* r) = 0;
  virtual void Visit(CircleShape* r) = 0;
  virtual void Visit(RectangleShape* r) = 0;
  virtual void Visit(RoundedRectangleShape* r) = 0;
  virtual void Visit(Material* r) = 0;
  virtual void Visit(Import* r) = 0;
};

}  // namespace scene
}  // namespace mozart
