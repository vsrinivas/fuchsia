// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace mozart {
namespace composer {

class EntityNode;
class Node;
class ShapeNode;
class CircleShape;
class Shape;
class Link;
class Material;

class ResourceVisitor {
 public:
  virtual void Visit(EntityNode* r) = 0;
  virtual void Visit(Node* r) = 0;
  virtual void Visit(ShapeNode* r) = 0;
  virtual void Visit(CircleShape* r) = 0;
  virtual void Visit(Shape* r) = 0;
  virtual void Visit(Link* r) = 0;
  virtual void Visit(Material* r) = 0;
};

}  // namespace composer
}  // namespace mozart
