// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/ui/scenic/client/session.h"

#include "lib/fxl/macros.h"

namespace scenic_lib {

// Represents a resource in a session with a dynamically allocated id.
// The resource is released from the session when this object is destroyed
// but it may still be in use within the session if other resources reference
// it.
// This type cannot be instantiated, please see subclasses.
class Resource {
 public:
  // Gets the session which owns this resource.
  Session* session() const {
    FXL_DCHECK(session_);
    return session_;
  }

  // Gets the resource's id.
  uint32_t id() const { return id_; }

  // Exports the resource and associates it with |export_token|.
  void Export(zx::eventpair export_token);

  // Exports the resource and returns an import token in |out_import_token|
  // which allows it to be imported into other sessions.
  void ExportAsRequest(zx::eventpair* out_import_token);

  // Sets which events a resource should deliver to the session listener.
  void SetEventMask(uint32_t event_mask);

  // Sets a label to help developers identify the purpose of the resource
  // when using diagnostic tools.
  void SetLabel(const std::string& label);

 protected:
  explicit Resource(Session* session);
  Resource(Resource&& moved);

  ~Resource();

 private:
  Session* const session_;
  uint32_t const id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Resource);
};

// Represents a memory resource in a session.
// TODO(MZ-268): Make this class final, and add public move constructor.
class Memory : public Resource {
 public:
  Memory(Session* session, zx::vmo vmo, scenic::MemoryType memory_type);
  ~Memory();

  // Gets the underlying VMO's memory type, indicating whether it represents
  // host or GPU memory.
  scenic::MemoryType memory_type() const { return memory_type_; }

 protected:
  Memory(Memory&& moved);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Memory);

  scenic::MemoryType const memory_type_;
};

// Represents an abstract shape resource in a session.
// This type cannot be instantiated, please see subclasses.
class Shape : public Resource {
 protected:
  explicit Shape(Session* session);
  Shape(Shape&& moved);
  ~Shape();

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Shape);
};

// Represents a circle shape resource in a session.
class Circle final : public Shape {
 public:
  Circle(Session* session, float radius);
  Circle(Circle&& moved);
  ~Circle();

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Circle);
};

// Represents a rectangle shape resource in a session.
class Rectangle final : public Shape {
 public:
  Rectangle(Session* session, float width, float height);
  Rectangle(Rectangle&& moved);
  ~Rectangle();

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Rectangle);
};

// Represents a rounded rectangle shape resource in a session.
class RoundedRectangle final : public Shape {
 public:
  RoundedRectangle(Session* session,
                   float width,
                   float height,
                   float top_left_radius,
                   float top_right_radius,
                   float bottom_right_radius,
                   float bottom_left_radius);
  RoundedRectangle(RoundedRectangle&& moved);
  ~RoundedRectangle();

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(RoundedRectangle);
};

// Represents an image resource in a session.
// TODO(MZ-268): Make this class final, and add public move constructor.
class Image : public Resource {
 public:
  // Creates an image resource bound to a session.
  Image(const Memory& memory, off_t memory_offset, scenic::ImageInfoPtr info);
  Image(Session* session,
        uint32_t memory_id,
        off_t memory_offset,
        scenic::ImageInfoPtr info);
  ~Image();

  // Returns the number of bytes needed to represent an image.
  static size_t ComputeSize(const scenic::ImageInfo& image_info);

  // Gets the byte offset of the image within its memory resource.
  off_t memory_offset() const { return memory_offset_; }

  // Gets information about the image's layout.
  const scenic::ImageInfo& info() const { return info_; }

 protected:
  Image(Image&& moved);

 private:
  off_t const memory_offset_;
  scenic::ImageInfo const info_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Image);
};

// Represents a buffer that is immutably bound to a range of a memory resource.
class Buffer final : public Resource {
 public:
  Buffer(const Memory& memory, off_t memory_offset, size_t buffer_size);
  Buffer(Session* session,
         uint32_t memory_id,
         off_t memory_offset,
         size_t buffer_size);
  Buffer(Buffer&& moved);
  ~Buffer();

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Buffer);
};

// Represents a mesh resource in a session.  Before it can be rendered, it
// must be bound to index and vertex arrays by calling the BindBuffers() method.
class Mesh final : public Shape {
 public:
  Mesh(Session* session);
  Mesh(Mesh&& moved);
  ~Mesh();

  // These arguments are documented in ops.fidl; see BindMeshBuffersOp.
  void BindBuffers(const Buffer& index_buffer,
                   scenic::MeshIndexFormat index_format,
                   uint64_t index_offset,
                   uint32_t index_count,
                   const Buffer& vertex_buffer,
                   scenic::MeshVertexFormatPtr vertex_format,
                   uint64_t vertex_offset,
                   uint32_t vertex_count,
                   const float bounding_box_min[3],
                   const float bounding_box_max[3]);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Mesh);
};

// Represents a material resource in a session.
class Material final : public Resource {
 public:
  explicit Material(Session* session);
  Material(Material&& moved);
  ~Material();

  // Sets the material's texture.
  void SetTexture(const Image& image) { SetTexture(image.id()); }
  void SetTexture(uint32_t image_id);

  // Sets the material's color.
  void SetColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Material);
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

  // Sets the node's hit test behavior.
  void SetHitTestBehavior(scenic::HitTestBehavior hit_test_behavior);

  // Detaches the node from its parent.
  void Detach();

 protected:
  explicit Node(Session* session);
  Node(Node&& moved);
  ~Node();

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Node);
};

// Represents an shape node resource in a session.
class ShapeNode final : public Node {
 public:
  explicit ShapeNode(Session* session);
  ShapeNode(ShapeNode&& moved);
  ~ShapeNode();

  // Sets the shape that the shape node should draw.
  void SetShape(const Shape& shape) { SetShape(shape.id()); }
  void SetShape(uint32_t shape_id);

  // Sets the material with which to draw the shape.
  void SetMaterial(const Material& material) { SetMaterial(material.id()); }
  void SetMaterial(uint32_t material_id);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ShapeNode);
};

// Abstract base class for nodes which can have child nodes.
// This type cannot be instantiated, please see subclasses.
class ContainerNode : public Node {
 public:
  // Adds a child to the node.
  void AddChild(const Node& child) { AddChild(child.id()); }
  void AddChild(uint32_t child_node_id);

  void AddPart(const Node& part) { AddPart(part.id()); }
  void AddPart(uint32_t part_node_id);

  // Detaches all children from the node.
  void DetachChildren();

 protected:
  explicit ContainerNode(Session* session);
  ContainerNode(ContainerNode&& moved);
  ~ContainerNode();

  FXL_DISALLOW_COPY_AND_ASSIGN(ContainerNode);
};

// Represents an entity node resource in a session.
// TODO(MZ-268): Make this class final, and add public move constructor.
class EntityNode : public ContainerNode {
 public:
  explicit EntityNode(Session* session);
  ~EntityNode();

  void SetClip(uint32_t clip_id, bool clip_to_self);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(EntityNode);
};

// Represents an imported node resource in a session.
// The imported node is initially created in an unbound state and must
// be bound immediately after creation, prior to use.
class ImportNode final : public ContainerNode {
 public:
  explicit ImportNode(Session* session);
  ImportNode(ImportNode&& moved);
  ~ImportNode();

  // Imports the node associated with |import_token|.
  void Bind(zx::eventpair import_token);

  // Imports the node and returns an export token in |out_export_token|
  // by which another session can export a node to associate with this import.
  void BindAsRequest(zx::eventpair* out_export_token);

  // Returns true if the import has been bound.
  bool is_bound() const { return is_bound_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ImportNode);

  bool is_bound_ = false;
};

// Creates a node that clips the contents of its hierarchy to the specified clip
// shape.
class ClipNode final : public ContainerNode {
 public:
  explicit ClipNode(Session* session);
  ClipNode(ClipNode&& moved);
  ~ClipNode();

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ClipNode);
};

// Creates a node that renders its hierarchy with the specified opacity.
class OpacityNode final : public ContainerNode {
 public:
  explicit OpacityNode(Session* session);
  OpacityNode(OpacityNode&& moved);
  ~OpacityNode();

  // The opacity with which to render the contents of the hierarchy rooted at
  // this node. The opacity values are clamped 0.0 to 1.0.
  void SetOpacity(double opacity);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(OpacityNode);
};

// Represents a scene resource in a session.
class Scene final : public ContainerNode {
 public:
  explicit Scene(Session* session);
  Scene(Scene&& moved);
  ~Scene();

 private:
  void Detach() = delete;

  FXL_DISALLOW_COPY_AND_ASSIGN(Scene);
};

// Represents a camera resource in a session.
class Camera final : public Resource {
 public:
  explicit Camera(const Scene& scene);
  Camera(Session* session, uint32_t scene_id);
  Camera(Camera&& moved);
  ~Camera();

  // Sets the camera's projection parameters.
  void SetProjection(const float eye_position[3],
                     const float eye_look_at[3],
                     const float eye_up[3],
                     float fovy);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Camera);
};

// Represents a renderer resource in a session.
class Renderer final : public Resource {
 public:
  explicit Renderer(Session* session);
  Renderer(Renderer&& moved);
  ~Renderer();

  // Sets the camera whose view will be rendered.
  void SetCamera(const Camera& camera) { SetCamera(camera.id()); }
  void SetCamera(uint32_t camera_id);

  // Set whether clipping is disabled for this renderer.
  // NOTE: disabling clipping only has a visual effect; hit-testing is not
  // affected.
  void SetDisableClipping(bool disable_clipping);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Renderer);
};

// Represents a layer resource in a session.
class Layer final : public Resource {
 public:
  explicit Layer(Session* session);
  Layer(Layer&& moved);
  ~Layer();

  // Sets the layer's XY translation and Z-order.
  void SetTranslation(float tx, float ty, float tz) {
    SetTranslation((float[3]){tx, ty, tz});
  }
  void SetTranslation(const float translation[3]);

  void SetSize(float width, float height) {
    SetSize((float[2]){width, height});
  }
  void SetSize(const float size[2]);

  void SetRenderer(const Renderer& renderer) { SetRenderer(renderer.id()); }
  void SetRenderer(uint32_t renderer_id);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Layer);
};

// Represents a layer-stack resource in a session.
class LayerStack final : public Resource {
 public:
  explicit LayerStack(Session* session);
  LayerStack(LayerStack&& moved);
  ~LayerStack();

  void AddLayer(const Layer& layer) { AddLayer(layer.id()); }
  void AddLayer(uint32_t layer_id);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LayerStack);
};

// Represents a display-compositor resource in a session.
class DisplayCompositor final : public Resource {
 public:
  explicit DisplayCompositor(Session* session);
  DisplayCompositor(DisplayCompositor&& moved);
  ~DisplayCompositor();

  // Sets the layer-stack that is to be composited.
  void SetLayerStack(const LayerStack& layer_stack) {
    SetLayerStack(layer_stack.id());
  }
  void SetLayerStack(uint32_t layer_stack_id);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayCompositor);
};

}  // namespace scenic_lib
