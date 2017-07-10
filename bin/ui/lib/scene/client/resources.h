// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/lib/scene/client/session.h"

#include "lib/ftl/macros.h"

namespace mozart {
namespace client {

// Represents a resource in a session with a dynamically allocated id.
// The resource is released from the session when this object is destroyed
// but it may still be in use within the session if other resources reference
// it.
// This type cannot be instantiated, please see subclasses.
class Resource {
 public:
  // Gets the session which owns this resource.
  Session* session() const { return session_; }

  // Gets the resource's id.
  uint32_t id() const { return id_; }

  // Exports the resource and associates it with |export_token|.
  void Export(mx::eventpair export_token);

  // Exports the resource and returns an import token in |out_import_token|
  // which allows it to be imported into other sessions.
  void ExportAsRequest(mx::eventpair* out_import_token);

 protected:
  explicit Resource(Session* session);
  ~Resource();

  void AddChild(uint32_t child_node_id);
  void AddPart(uint32_t part_node_id);
  void DetachChildren();

 private:
  Session* const session_;
  uint32_t const id_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Resource);
};

// Represents a memory resource in a session.
class Memory : public Resource {
 public:
  explicit Memory(Session* session,
                  mx::vmo vmo,
                  mozart2::MemoryType memory_type);
  ~Memory();

  // Gets the underlying VMO's memory type, indicating whether it represents
  // host or GPU memory.
  mozart2::MemoryType memory_type() const { return memory_type_; }

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Memory);

  mozart2::MemoryType const memory_type_;
};

// Represents an abstract shape resource in a session.
// This type cannot be instantiated, please see subclasses.
class Shape : public Resource {
 protected:
  explicit Shape(Session* session);
  ~Shape();

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Shape);
};

// Represents a circle shape resource in a session.
class Circle : public Shape {
 public:
  explicit Circle(Session* session, float radius);
  ~Circle();

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Circle);
};

// Represents a rectangle shape resource in a session.
class Rectangle : public Shape {
 public:
  explicit Rectangle(Session* session, float width, float height);
  ~Rectangle();

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Rectangle);
};

// Represents a rounded rectangle shape resource in a session.
class RoundedRectangle : public Shape {
 public:
  explicit RoundedRectangle(Session* session,
                            float width,
                            float height,
                            float top_left_radius,
                            float top_right_radius,
                            float bottom_right_radius,
                            float bottom_left_radius);
  ~RoundedRectangle();

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(RoundedRectangle);
};

// Represents an image resource in a session.
class Image : public Resource {
 public:
  // Creates an image resource bound to a session.
  explicit Image(const Memory& memory,
                 off_t memory_offset,
                 mozart2::ImageInfoPtr info);
  explicit Image(Session* session,
                 uint32_t memory_id,
                 off_t memory_offset,
                 mozart2::ImageInfoPtr info);
  ~Image();

  // Returns the number of bytes needed to represent an image.
  static size_t ComputeSize(const mozart2::ImageInfo& image_info);

  // Gets the byte offset of the image within its memory resource.
  off_t memory_offset() const { return memory_offset_; }

  // Gets information about the image's layout.
  const mozart2::ImageInfo& info() const { return info_; }

 private:
  off_t const memory_offset_;
  mozart2::ImageInfo const info_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Image);
};

// Represents a material resource in a session.
class Material : public Resource {
 public:
  explicit Material(Session* session);
  ~Material();

  // Sets the material's texture.
  void SetTexture(const Image& image) { SetTexture(image.id()); }
  void SetTexture(uint32_t image_id);

  // Sets the material's color.
  void SetColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Material);
};

// Represents an abstract node resource in a session.
// This type cannot be instantiated, please see subclasses.
class Node : public Resource {
 public:
  // Sets the node's transform properties.
  void SetTranslation(float tx, float ty, float tz) {
    SetTranslation((float[3]){tx, ty, tz});
  }
  void SetTranslation(const float translation[3]);
  void SetScale(float sx, float sy, float sz) {
    SetScale((float[3]){sx, sy, sz});
  }
  void SetScale(const float scale[3]);
  void SetRotation(float qi, float qj, float qk, float qw) {
    SetRotation((float[4]){qi, qj, qk, qw});
  }
  void SetRotation(const float quaternion[4]);
  void SetAnchor(float ax, float ay, float az) {
    SetAnchor((float[3]){ax, ay, az});
  }
  void SetAnchor(const float anchor[3]);

  // Sets the node's tag value.
  void SetTag(uint32_t tag_value);

  // Detaches the node from its parent.
  void Detach();

 protected:
  explicit Node(Session* session);
  ~Node();

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Node);
};

// Represents an shape node resource in a session.
class ShapeNode : public Node {
 public:
  explicit ShapeNode(Session* session);
  ~ShapeNode();

  // Sets the shape that the shape node should draw.
  void SetShape(const Shape& shape) { SetShape(shape.id()); }
  void SetShape(uint32_t shape_id);

  // Sets the material with which to draw the shape.
  void SetMaterial(const Material& material) { SetMaterial(material.id()); }
  void SetMaterial(uint32_t material_id);

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(ShapeNode);
};

// Traits for resources which can have child nodes.
// This type cannot be instantiated, please see subclasses.
template <typename Base>
class ContainerTraits : public Base {
 public:
  // Adds a child to the node.
  void AddChild(const Node& child) { AddChild(child.id()); }
  void AddChild(uint32_t child_node_id) { Base::AddChild(child_node_id); }

  void AddPart(const Node& child) { AddPart(child.id()); }
  void AddPart(uint32_t child_node_id) { Base::AddPart(child_node_id); }

  // Detaches all children from the node.
  void DetachChildren() { Base::DetachChildren(); }

 protected:
  explicit ContainerTraits(Session* session) : Base(session) {}
  ~ContainerTraits() = default;
};

// Abstract base class for nodes which can have child nodes.
// This type cannot be instantiated, please see subclasses.
using ContainerNode = mozart::client::ContainerTraits<mozart::client::Node>;

// Represents an entity node resource in a session.
class EntityNode : public ContainerNode {
 public:
  explicit EntityNode(Session* session);
  ~EntityNode();

  void SetClip(uint32_t clip_id, bool clip_to_self);

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(EntityNode);
};

// Represents an imported node resource in a session.
// The imported node is initially created in an unbound state and must
// be bound immediately after creation, prior to use.
class ImportNode : public ContainerNode {
 public:
  explicit ImportNode(Session* session);
  ~ImportNode();

  // Imports the node associated with |import_token|.
  void Bind(mx::eventpair import_token);

  // Imports the node and returns an export token in |out_export_token|
  // by which another session can export a node to associate with this import.
  void BindAsRequest(mx::eventpair* out_export_token);

  // Returns true if the import has been bound.
  bool is_bound() const { return is_bound_; }

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(ImportNode);

  bool is_bound_ = false;
};

// Creates a node that clips the contents of its hierarchy to the specified clip
// shape.
class ClipNode : public ContainerNode {
 public:
  explicit ClipNode(Session* session);
  ~ClipNode();

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(ClipNode);
};

// Creates a node that renders its hierarchy with the specified opacity.
class OpacityNode : public ContainerNode {
 public:
  explicit OpacityNode(Session* session);
  ~OpacityNode();

  // The opacity with which to render the contents of the hierarchy rooted at
  // this node. The opacity values are clamped 0.0 to 1.0.
  void SetOpacity(double opacity);

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(OpacityNode);
};

// Represents a scene resource is a session.
class Scene : public ContainerTraits<Resource> {
 public:
  explicit Scene(Session* session);
  ~Scene();

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Scene);
};

// Represents a camera resource is a session.
class Camera : public Resource {
 public:
  explicit Camera(const Scene& scene);
  explicit Camera(Session* session, uint32_t scene_id);
  ~Camera();

  // Sets the camera's projection parameters.
  void SetProjection(const float eye_position[3],
                     const float eye_look_at[3],
                     const float eye_up[3],
                     float fovy);

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Camera);
};

// Represents a display renderer resource is a session.
class DisplayRenderer : public Resource {
 public:
  explicit DisplayRenderer(Session* session);
  ~DisplayRenderer();

  // Sets the camera whose view will be rendered.
  void SetCamera(const Camera& camera) { SetCamera(camera.id()); }
  void SetCamera(uint32_t camera_id);

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(DisplayRenderer);
};

}  // namespace client
}  // namespace mozart
