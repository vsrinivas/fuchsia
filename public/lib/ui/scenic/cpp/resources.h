// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CPP_RESOURCES_H_
#define LIB_UI_SCENIC_CPP_RESOURCES_H_

#include "lib/ui/scenic/cpp/session.h"

#include <zircon/assert.h>

namespace scenic {

// Represents a resource in a session with a dynamically allocated id.
// The resource is released from the session when this object is destroyed
// but it may still be in use within the session if other resources reference
// it.
// This type cannot be instantiated, please see subclasses.
class Resource {
 public:
  // Gets the session which owns this resource.
  Session* session() const {
    ZX_DEBUG_ASSERT(session_);
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

  Resource(const Resource&) = delete;
  Resource& operator=(const Resource&) = delete;

  virtual ~Resource();

 private:
  Session* const session_;
  uint32_t const id_;
};

// Represents a memory resource in a session.
// TODO(MZ-268): Make this class final, and add public move constructor.
class Memory : public Resource {
 public:
  Memory(Session* session, zx::vmo vmo, uint64_t allocation_size,
         fuchsia::images::MemoryType memory_type);
  ~Memory();

  // Gets the underlying VMO's memory type, indicating whether it represents
  // host or GPU memory.
  fuchsia::images::MemoryType memory_type() const { return memory_type_; }

 protected:
  Memory(Memory&& moved);

 private:
  fuchsia::images::MemoryType const memory_type_;
};

// Represents an abstract shape resource in a session.
// This type cannot be instantiated, please see subclasses.
class Shape : public Resource {
 protected:
  explicit Shape(Session* session);
  Shape(Shape&& moved);
  ~Shape();
};

// Represents a circle shape resource in a session.
class Circle final : public Shape {
 public:
  Circle(Session* session, float radius);
  Circle(Circle&& moved);
  ~Circle();
};

// Represents a rectangle shape resource in a session.
class Rectangle final : public Shape {
 public:
  Rectangle(Session* session, float width, float height);
  Rectangle(Rectangle&& moved);
  ~Rectangle();
};

// Represents a rounded rectangle shape resource in a session.
class RoundedRectangle final : public Shape {
 public:
  RoundedRectangle(Session* session, float width, float height,
                   float top_left_radius, float top_right_radius,
                   float bottom_right_radius, float bottom_left_radius);
  RoundedRectangle(RoundedRectangle&& moved);
  ~RoundedRectangle();
};

// Represents an image resource in a session.
// TODO(MZ-268): Make this class final, and add public move constructor.
class Image : public Resource {
 public:
  // Creates an image resource bound to a session.
  Image(const Memory& memory, off_t memory_offset,
        fuchsia::images::ImageInfo info);
  Image(Session* session, uint32_t memory_id, off_t memory_offset,
        fuchsia::images::ImageInfo info);
  ~Image();

  // Returns the number of bytes needed to represent an image.
  static size_t ComputeSize(const fuchsia::images::ImageInfo& image_info);

  // Gets the byte offset of the image within its memory resource.
  off_t memory_offset() const { return memory_offset_; }

  // Gets information about the image's layout.
  const fuchsia::images::ImageInfo& info() const { return info_; }

 protected:
  Image(Image&& moved);

 private:
  off_t const memory_offset_;
  fuchsia::images::ImageInfo const info_;
};

// Represents a buffer that is immutably bound to a range of a memory resource.
class Buffer final : public Resource {
 public:
  Buffer(const Memory& memory, off_t memory_offset, size_t buffer_size);
  Buffer(Session* session, uint32_t memory_id, off_t memory_offset,
         size_t buffer_size);
  Buffer(Buffer&& moved);
  ~Buffer();
};

// Represents a mesh resource in a session.  Before it can be rendered, it
// must be bound to index and vertex arrays by calling the BindBuffers() method.
class Mesh final : public Shape {
 public:
  Mesh(Session* session);
  Mesh(Mesh&& moved);
  ~Mesh();

  // These arguments are documented in commands.fidl; see
  // BindMeshBuffersCmd.
  void BindBuffers(const Buffer& index_buffer,
                   fuchsia::ui::gfx::MeshIndexFormat index_format,
                   uint64_t index_offset, uint32_t index_count,
                   const Buffer& vertex_buffer,
                   fuchsia::ui::gfx::MeshVertexFormat vertex_format,
                   uint64_t vertex_offset, uint32_t vertex_count,
                   const float bounding_box_min[3],
                   const float bounding_box_max[3]);
};

// Represents a material resource in a session.
class Material final : public Resource {
 public:
  explicit Material(Session* session);
  Material(Material&& moved);
  ~Material();

  // Sets the material's texture.
  void SetTexture(const Image& image) {
    ZX_DEBUG_ASSERT(session() == image.session());
    SetTexture(image.id());
  }
  void SetTexture(uint32_t image_id);

  // Sets the material's color.
  void SetColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
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
  void SetTranslation(uint32_t variable_id);
  void SetScale(float sx, float sy, float sz) {
    SetScale((float[3]){sx, sy, sz});
  }
  void SetScale(const float scale[3]);
  void SetScale(uint32_t variable_id);
  void SetRotation(float qi, float qj, float qk, float qw) {
    SetRotation((float[4]){qi, qj, qk, qw});
  }
  void SetRotation(const float quaternion[4]);
  void SetRotation(uint32_t variable_id);
  void SetAnchor(float ax, float ay, float az) {
    SetAnchor((float[3]){ax, ay, az});
  }
  void SetAnchor(const float anchor[3]);
  void SetAnchor(uint32_t variable_id);

  void SendSizeChangeHint(float width_change_factor,
                          float height_change_factor);

  // Sets the node's tag value.
  void SetTag(uint32_t tag_value);

  // Sets the node's hit test behavior.
  void SetHitTestBehavior(fuchsia::ui::gfx::HitTestBehavior hit_test_behavior);

  // Detaches the node from its parent.
  void Detach();

 protected:
  explicit Node(Session* session);
  Node(Node&& moved);
  ~Node();
};

// Represents an shape node resource in a session.
class ShapeNode final : public Node {
 public:
  explicit ShapeNode(Session* session);
  ShapeNode(ShapeNode&& moved);
  ~ShapeNode();

  // Sets the shape that the shape node should draw.
  void SetShape(const Shape& shape) {
    ZX_DEBUG_ASSERT(session() == shape.session());
    SetShape(shape.id());
  }
  void SetShape(uint32_t shape_id);

  // Sets the material with which to draw the shape.
  void SetMaterial(const Material& material) {
    ZX_DEBUG_ASSERT(session() == material.session());
    SetMaterial(material.id());
  }
  void SetMaterial(uint32_t material_id);
};

// Abstract base class for nodes which can have child nodes.
// This type cannot be instantiated, please see subclasses.
class ContainerNode : public Node {
 public:
  // Adds a child to the node.
  void AddChild(const Node& child) {
    ZX_DEBUG_ASSERT(session() == child.session());
    AddChild(child.id());
  }
  void AddChild(uint32_t child_node_id);

  void AddPart(const Node& part) {
    ZX_DEBUG_ASSERT(session() == part.session());
    AddPart(part.id());
  }
  void AddPart(uint32_t part_node_id);

  // Detaches all children from the node.
  void DetachChildren();

 protected:
  explicit ContainerNode(Session* session);
  ContainerNode(ContainerNode&& moved);
  ~ContainerNode();
};

// Required by EntityNode::Attach().
class ViewHolder;

// Represents an entity node resource in a session.
// TODO(MZ-268): Make this class final, and add public move constructor.
class EntityNode : public ContainerNode {
 public:
  explicit EntityNode(Session* session);
  ~EntityNode();

  void SetClip(uint32_t clip_id, bool clip_to_self);

  void Attach(const ViewHolder& view_holder);

  void Snapshot(fuchsia::ui::gfx::SnapshotCallbackHACKPtr callback);
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
  bool is_bound_ = false;
};

// Represents a proxy for a View which can be added to a scene graph in order
// to embed the View within it.
//
// Each ViewHolder is linked to a paired View via a shared token.
//
// Usually the ViewHolder and its associated View exist in separate processes,
// allowing a distributed scene graph to be constructed.
class ViewHolder final : public Resource {
 public:
  ViewHolder(Session* session, zx::eventpair token,
             const std::string& debug_name);
  ~ViewHolder();

  // Set properties of the attached view.

  void SetViewProperties(float min_x, float min_y, float min_z, float max_x,
                         float max_y, float max_z, float in_min_x,
                         float in_min_y, float in_min_z, float in_max_x,
                         float in_max_y, float in_max_z) {
    SetViewProperties((float[3]){min_x, min_y, min_z},
                      (float[3]){max_x, max_y, max_z},
                      (float[3]){in_min_x, in_min_y, in_min_z},
                      (float[3]){in_max_x, in_max_y, in_max_z});
  }
  void SetViewProperties(const float bounding_box_min[3],
                         const float bounding_box_max[3],
                         const float inset_from_min[3],
                         const float inset_from_max[3]);
  void SetViewProperties(const fuchsia::ui::gfx::ViewProperties& props);
};

// Represents a transform space which serves as a container for Nodes.  The
// Nodes will have the Views' coordinate transform applied to their own, in
// addition to being clipped to the Views' bounding box.
class View final : public Resource {
 public:
  View(Session* session, zx::eventpair token, const std::string& debug_name);
  ~View();

  void AddChild(const Node& child) const;
  void DetachChild(const Node& child) const;
};

// Creates a node that clips the contents of its hierarchy to the specified clip
// shape.
class ClipNode final : public ContainerNode {
 public:
  explicit ClipNode(Session* session);
  ClipNode(ClipNode&& moved);
  ~ClipNode();
};

// Creates a node that renders its hierarchy with the specified opacity.
class OpacityNode final : public ContainerNode {
 public:
  explicit OpacityNode(Session* session);
  OpacityNode(OpacityNode&& moved);
  ~OpacityNode();

  // The opacity with which to render the contents of the hierarchy rooted at
  // this node. The opacity values are clamped 0.0 to 1.0.
  void SetOpacity(float opacity);
};

// A value that can be used in place of a constant value.
class Variable final : public Resource {
 public:
  explicit Variable(Session* session, fuchsia::ui::gfx::Value initial_value);
  Variable(Variable&& moved);
  ~Variable();
};

// Represents an abstract light resource in a session.
// This type cannot be instantiated, please see subclasses.
class Light : public Resource {
 public:
  // Sets the light's color.
  void SetColor(float red, float green, float blue) {
    SetColor((float[3]){red, green, blue});
  }
  void SetColor(const float rgb[3]);
  void SetColor(uint32_t variable_id);

  // Detach light from the scene it is attached to, if any.
  void Detach();

 protected:
  explicit Light(Session* session);
  Light(Light&& moved);
  ~Light();
};

// Represents a directional light resource in a session.
class AmbientLight final : public Light {
 public:
  explicit AmbientLight(Session* session);
  AmbientLight(AmbientLight&& moved);
  ~AmbientLight();
};

// Represents a directional light resource in a session.
class DirectionalLight final : public Light {
 public:
  explicit DirectionalLight(Session* session);
  DirectionalLight(DirectionalLight&& moved);
  ~DirectionalLight();

  // Sets the light's direction.
  void SetDirection(float dx, float dy, float dz) {
    SetDirection((float[3]){dx, dy, dz});
  }
  void SetDirection(const float direction[3]);
  void SetDirection(uint32_t variable_id);
};

// Represents a scene resource in a session.
class Scene final : public ContainerNode {
 public:
  explicit Scene(Session* session);
  Scene(Scene&& moved);
  ~Scene();

  void AddLight(const Light& light) {
    ZX_DEBUG_ASSERT(session() == light.session());
    AddLight(light.id());
  }
  void AddLight(uint32_t light_id);
  void DetachLights();

 private:
  void Detach() = delete;
};

class CameraBase : public Resource {
 public:
  CameraBase(Session* session) : Resource(session) {}
  CameraBase(CameraBase&& moved) : Resource(std::move(moved)) {}
  ~CameraBase() {}
  // Sets the camera's view parameters.
  void SetTransform(const float eye_position[3], const float eye_look_at[3],
                    const float eye_up[3]);
  // Sets the camera pose buffer
  void SetPoseBuffer(const Buffer& buffer, uint32_t num_entries,
                     int64_t base_time, uint64_t time_interval);
};

// Represents a camera resource in a session.
class Camera : public CameraBase {
 public:
  explicit Camera(const Scene& scene);
  Camera(Session* session, uint32_t scene_id);
  Camera(Camera&& moved);
  ~Camera();

  // Sets the camera's projection parameters.
  void SetProjection(const float fovy);
};

// Represents a StereoCamera resource in a session.
class StereoCamera final : public CameraBase {
 public:
  explicit StereoCamera(const Scene& scene);
  StereoCamera(Session* session, uint32_t scene_id);
  StereoCamera(StereoCamera&& moved);
  ~StereoCamera();

  // Sets the camera's projection parameters.
  void SetStereoProjection(const float left_projection[16],
                           const float right_projection[16]);
};

// Represents a renderer resource in a session.
class Renderer final : public Resource {
 public:
  explicit Renderer(Session* session);
  Renderer(Renderer&& moved);
  ~Renderer();

  // Sets the camera whose view will be rendered.
  void SetCamera(const Camera& camera) {
    ZX_DEBUG_ASSERT(session() == camera.session());
    SetCamera(camera.id());
  }
  void SetCamera(uint32_t camera_id);

  void SetParam(fuchsia::ui::gfx::RendererParam param);

  // Convenient wrapper for SetParam().
  void SetShadowTechnique(fuchsia::ui::gfx::ShadowTechnique technique);

  // Set whether clipping is disabled for this renderer.
  // NOTE: disabling clipping only has a visual effect; hit-testing is not
  // affected.
  void SetDisableClipping(bool disable_clipping);
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

  void SetRenderer(const Renderer& renderer) {
    ZX_DEBUG_ASSERT(session() == renderer.session());
    SetRenderer(renderer.id());
  }
  void SetRenderer(uint32_t renderer_id);
};

// Represents a layer-stack resource in a session.
class LayerStack final : public Resource {
 public:
  explicit LayerStack(Session* session);
  LayerStack(LayerStack&& moved);
  ~LayerStack();

  void AddLayer(const Layer& layer) {
    ZX_DEBUG_ASSERT(session() == layer.session());
    AddLayer(layer.id());
  }
  void AddLayer(uint32_t layer_id);
  void RemoveLayer(const Layer& layer) {
    ZX_DEBUG_ASSERT(session() == layer.session());
    RemoveLayer(layer.id());
  }
  void RemoveLayer(uint32_t layer_id);
  void RemoveAllLayers();
};

// Represents a display-compositor resource in a session.
class DisplayCompositor final : public Resource {
 public:
  explicit DisplayCompositor(Session* session);
  DisplayCompositor(DisplayCompositor&& moved);
  ~DisplayCompositor();

  // Sets the layer-stack that is to be composited.
  void SetLayerStack(const LayerStack& layer_stack) {
    ZX_DEBUG_ASSERT(session() == layer_stack.session());
    SetLayerStack(layer_stack.id());
  }
  void SetLayerStack(uint32_t layer_stack_id);
};

// Represents a display-less compositor resource in a session.
class Compositor final : public Resource {
 public:
  explicit Compositor(Session* session);
  Compositor(Compositor&& moved);
  ~Compositor();

  // Sets the layer-stack that is to be composited.
  void SetLayerStack(const LayerStack& layer_stack) {
    ZX_DEBUG_ASSERT(session() == layer_stack.session());
    SetLayerStack(layer_stack.id());
  }
  void SetLayerStack(uint32_t layer_stack_id);
};

}  // namespace scenic

#endif  // LIB_UI_SCENIC_CPP_RESOURCES_H_
