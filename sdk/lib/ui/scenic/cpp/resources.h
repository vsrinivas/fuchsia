// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CPP_RESOURCES_H_
#define LIB_UI_SCENIC_CPP_RESOURCES_H_

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>

#include <array>

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
  Resource(Resource&& moved) noexcept;

  Resource(const Resource&) = delete;
  Resource& operator=(const Resource&) = delete;

  virtual ~Resource();

 private:
  Session* const session_;
  uint32_t const id_;
};

// Represents a memory resource in a session.
// TODO(SCN-268): Make this class final, and add public move constructor.
class Memory : public Resource {
 public:
  Memory(Session* session, zx::vmo vmo, uint64_t allocation_size,
         fuchsia::images::MemoryType memory_type);
  ~Memory();

  // Gets the underlying VMO's memory type, indicating whether it represents
  // host or GPU memory.
  fuchsia::images::MemoryType memory_type() const { return memory_type_; }

 protected:
  Memory(Memory&& moved) noexcept;

 private:
  fuchsia::images::MemoryType const memory_type_;
};

// Represents an abstract shape resource in a session.
// This type cannot be instantiated, please see subclasses.
class Shape : public Resource {
 protected:
  explicit Shape(Session* session);
  Shape(Shape&& moved) noexcept;
  ~Shape();
};

// Represents a circle shape resource in a session.
class Circle final : public Shape {
 public:
  Circle(Session* session, float radius);
  Circle(Circle&& moved) noexcept;
  ~Circle();
};

// Represents a rectangle shape resource in a session.
class Rectangle final : public Shape {
 public:
  Rectangle(Session* session, float width, float height);
  Rectangle(Rectangle&& moved) noexcept;
  ~Rectangle();
};

// Represents a rounded rectangle shape resource in a session.
class RoundedRectangle final : public Shape {
 public:
  RoundedRectangle(Session* session, float width, float height, float top_left_radius,
                   float top_right_radius, float bottom_right_radius, float bottom_left_radius);
  RoundedRectangle(RoundedRectangle&& moved) noexcept;
  ~RoundedRectangle();
};

// Represents an image resource in a session.
// TODO(SCN-268): Make this class final, and add public move constructor.
class Image : public Resource {
 public:
  // Creates an image resource bound to a session.
  Image(const Memory& memory, off_t memory_offset, fuchsia::images::ImageInfo info);
  Image(Session* session, uint32_t memory_id, off_t memory_offset, fuchsia::images::ImageInfo info);
  ~Image();

  // Returns the number of bytes needed to represent an image.
  static size_t ComputeSize(const fuchsia::images::ImageInfo& image_info);

  // Gets the byte offset of the image within its memory resource.
  off_t memory_offset() const { return memory_offset_; }

  // Gets information about the image's layout.
  const fuchsia::images::ImageInfo& info() const { return info_; }

 protected:
  Image(Image&& moved) noexcept;

 private:
  off_t const memory_offset_;
  fuchsia::images::ImageInfo const info_;
};

// Represents a buffer that is immutably bound to a range of a memory resource.
class Buffer final : public Resource {
 public:
  Buffer(const Memory& memory, off_t memory_offset, size_t num_bytes);
  Buffer(Session* session, uint32_t memory_id, off_t memory_offset, size_t num_bytes);
  Buffer(Buffer&& moved) noexcept;
  ~Buffer();
};

// Represents a mesh resource in a session.  Before it can be rendered, it
// must be bound to index and vertex arrays by calling the BindBuffers() method.
class Mesh final : public Shape {
 public:
  Mesh(Session* session);
  Mesh(Mesh&& moved) noexcept;

  ~Mesh();

  // These arguments are documented in commands.fidl; see
  // BindMeshBuffersCmd.
  void BindBuffers(const Buffer& index_buffer, fuchsia::ui::gfx::MeshIndexFormat index_format,
                   uint64_t index_offset, uint32_t index_count, const Buffer& vertex_buffer,
                   fuchsia::ui::gfx::MeshVertexFormat vertex_format, uint64_t vertex_offset,
                   uint32_t vertex_count, const std::array<float, 3>& bounding_box_min,
                   const std::array<float, 3>& bounding_box_max);
};

// Represents a material resource in a session.
class Material final : public Resource {
 public:
  explicit Material(Session* session);
  Material(Material&& moved) noexcept;
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
  void SetTranslation(float tx, float ty, float tz) { SetTranslation({tx, ty, tz}); }

  void SetTranslation(const std::array<float, 3>& translation);

  void SetTranslation(uint32_t variable_id);

  void SetScale(float sx, float sy, float sz) { SetScale({sx, sy, sz}); }
  void SetScale(const std::array<float, 3>& scale);
  void SetScale(uint32_t variable_id);
  void SetRotation(float qi, float qj, float qk, float qw) { SetRotation({qi, qj, qk, qw}); }
  void SetRotation(const std::array<float, 4>& quaternion);
  void SetRotation(uint32_t variable_id);
  void SetAnchor(float ax, float ay, float az) { SetAnchor({ax, ay, az}); }
  void SetAnchor(const std::array<float, 3>& anchor);
  void SetAnchor(uint32_t variable_id);

  void SendSizeChangeHint(float width_change_factor, float height_change_factor);

  // Sets the node's tag value.
  void SetTag(uint32_t tag_value);

  // Sets the node's hit test behavior.
  void SetHitTestBehavior(fuchsia::ui::gfx::HitTestBehavior hit_test_behavior);

  // Set the node's semantic visibility.
  void SetSemanticVisibility(bool visible);

  // Detaches the node from its parent.
  void Detach();

 protected:
  explicit Node(Session* session);
  Node(Node&& moved) noexcept;
  ~Node();
};

// Represents an shape node resource in a session.
class ShapeNode final : public Node {
 public:
  explicit ShapeNode(Session* session);
  ShapeNode(ShapeNode&& moved) noexcept;
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

  // Detaches all children from the node.
  void DetachChildren();

 protected:
  explicit ContainerNode(Session* session);
  ContainerNode(ContainerNode&& moved) noexcept;
  ~ContainerNode();
};

// Required by EntityNode::Attach().
class ViewHolder;

// Represents an entity node resource in a session.
// TODO(SCN-268): Make this class final, and add public move constructor.
class EntityNode : public ContainerNode {
 public:
  explicit EntityNode(Session* session);
  EntityNode(EntityNode&& moved) noexcept;
  ~EntityNode();

  void SetClip(uint32_t clip_id, bool clip_to_self);
  void SetClipPlanes(std::vector<fuchsia::ui::gfx::Plane3> planes);

  // Deprecated(38480): use |AddChild| instead.
  void Attach(const ViewHolder& view_holder);
};

// Represents an imported node resource in a session.
// The imported node is initially created in an unbound state and must
// be bound immediately after creation, prior to use.
//
// Deprecated(38480): use EntityNode instead or consider omitting.
class ImportNode final : public ContainerNode {
 public:
  explicit ImportNode(Session* session);
  ImportNode(ImportNode&& moved) noexcept;
  ~ImportNode();

  // Imports the node associated with |import_token|.
  void Bind(zx::eventpair import_token);

  // Imports the node and returns an export token in |out_export_token|
  // by which another session can export a node to associate with this import.
  void BindAsRequest(zx::eventpair* out_export_token);

  // Returns true if the import has been bound.
  bool is_bound() const { return is_bound_; }

  void Attach(const ViewHolder& view_holder);

 private:
  bool is_bound_ = false;
};

/// Represents an attachment point for a subgraph within a larger scene graph.
/// The |ViewHolder| can be attached to a Node as a child, and the contents of
/// the linked |View| will become a child of the Node as well.
///
/// Each |ViewHolder| is linked to a paired |View| via a shared token pair.
class ViewHolder final : public Node {
 public:
  ViewHolder(Session* session, zx::eventpair token, const std::string& debug_name);
  ViewHolder(Session* session, fuchsia::ui::views::ViewHolderToken token,
             const std::string& debug_name);
  ViewHolder(ViewHolder&& moved) noexcept;
  ~ViewHolder();

  // Set properties of the attached view.

  void SetViewProperties(float min_x, float min_y, float min_z, float max_x, float max_y,
                         float max_z, float in_min_x, float in_min_y, float in_min_z,
                         float in_max_x, float in_max_y, float in_max_z) {
    SetViewProperties({min_x, min_y, min_z}, {max_x, max_y, max_z}, {in_min_x, in_min_y, in_min_z},
                      {in_max_x, in_max_y, in_max_z});
  }
  void SetViewProperties(const std::array<float, 3>& bounding_box_min,
                         const std::array<float, 3>& bounding_box_max,
                         const std::array<float, 3>& inset_from_min,
                         const std::array<float, 3>& inset_from_max);
  void SetViewProperties(const fuchsia::ui::gfx::ViewProperties& props);

  void SetDebugBoundsColor(uint8_t red, uint8_t green, uint8_t blue);
};

// Represents the root of a subgraph within a larger scene graph.  |Node|s can
// be attached to the |View| as children, and these |Node|s will have the
// |View|s' coordinate transform applied to their own, in addition to being
// clipped to the |View|s' bounding box.
//
// Each |View| is linked to an associated |ViewHolder| via a shared token pair.
class View final : public Resource {
 public:
  View(Session* session, zx::eventpair token, const std::string& debug_name);
  View(Session* session, fuchsia::ui::views::ViewToken token, const std::string& debug_name);
  View(Session* session, fuchsia::ui::views::ViewToken token,
       fuchsia::ui::views::ViewRefControl control_ref, fuchsia::ui::views::ViewRef view_ref,
       const std::string& debug_name);
  View(View&& moved) noexcept;
  ~View();

  void AddChild(const Node& child) const;
  void DetachChild(const Node& child) const;

  void enableDebugBounds(bool enable);
};

// Creates a node that clips the contents of its hierarchy to the specified clip
// shape.
class ClipNode final : public ContainerNode {
 public:
  explicit ClipNode(Session* session);
  ClipNode(ClipNode&& moved) noexcept;
  ~ClipNode();
};

// Creates a node that renders its hierarchy with the specified opacity.
class OpacityNodeHACK final : public ContainerNode {
 public:
  explicit OpacityNodeHACK(Session* session);
  OpacityNodeHACK(OpacityNodeHACK&& moved) noexcept;
  ~OpacityNodeHACK();

  // The opacity with which to render the contents of the hierarchy rooted at
  // this node. The opacity values are clamped 0.0 to 1.0.
  void SetOpacity(float opacity);
};

// A value that can be used in place of a constant value.
class Variable final : public Resource {
 public:
  explicit Variable(Session* session, fuchsia::ui::gfx::Value initial_value);
  Variable(Variable&& moved) noexcept;
  ~Variable();
};

// Represents an abstract light resource in a session.
// This type cannot be instantiated, please see subclasses.
class Light : public Resource {
 public:
  // Sets the light's color.
  void SetColor(float red, float green, float blue) { SetColor({red, green, blue}); }
  void SetColor(const std::array<float, 3>& rgb);
  void SetColor(uint32_t variable_id);

  // Detach light from the scene it is attached to, if any.
  void Detach();

 protected:
  explicit Light(Session* session);
  Light(Light&& moved) noexcept;
  ~Light();
};

// Represents a directional light resource in a session.
class AmbientLight final : public Light {
 public:
  explicit AmbientLight(Session* session);
  AmbientLight(AmbientLight&& moved) noexcept;
  ~AmbientLight();
};

// Represents a directional light resource in a session.
class DirectionalLight final : public Light {
 public:
  explicit DirectionalLight(Session* session);
  DirectionalLight(DirectionalLight&& moved) noexcept;
  ~DirectionalLight();

  // Sets the light's direction.
  void SetDirection(float dx, float dy, float dz) { SetDirection({dx, dy, dz}); }
  void SetDirection(const std::array<float, 3>& direction);
  void SetDirection(uint32_t variable_id);
};

// Represents a point light resource in a session.
class PointLight final : public Light {
 public:
  explicit PointLight(Session* session);
  PointLight(PointLight&& moved) noexcept;
  ~PointLight();

  // Sets the light's direction.
  void SetPosition(float dx, float dy, float dz) { SetPosition({dx, dy, dz}); }
  void SetPosition(const std::array<float, 3>& position);
  void SetPosition(uint32_t variable_id);

  // Set the light's falloff.
  void SetFalloff(float falloff);
};

// Represents a scene resource in a session.
class Scene final : public ContainerNode {
 public:
  explicit Scene(Session* session);
  Scene(Scene&& moved) noexcept;
  ~Scene();

  void AddLight(const Light& light) {
    ZX_DEBUG_ASSERT(session() == light.session());
    AddLight(light.id());
  }
  void AddLight(uint32_t light_id);

  void AddAmbientLight(const AmbientLight& light) {
    ZX_DEBUG_ASSERT(session() == light.session());
    AddAmbientLight(light.id());
  }
  void AddAmbientLight(uint32_t light_id);

  void AddDirectionalLight(const DirectionalLight& light) {
    ZX_DEBUG_ASSERT(session() == light.session());
    AddDirectionalLight(light.id());
  }
  void AddDirectionalLight(uint32_t light_id);

  void AddPointLight(const PointLight& light) {
    ZX_DEBUG_ASSERT(session() == light.session());
    AddPointLight(light.id());
  }
  void AddPointLight(uint32_t light_id);

  void DetachLights();

 private:
  void Detach() = delete;
};

class CameraBase : public Resource {
 public:
  CameraBase(Session* session) : Resource(session) {}
  CameraBase(CameraBase&& moved) noexcept : Resource(std::move(moved)) {}
  ~CameraBase() {}
  // Sets the camera's view parameters.
  void SetTransform(const std::array<float, 3>& eye_position,
                    const std::array<float, 3>& eye_look_at, const std::array<float, 3>& eye_up);
  // Sets the camera's 2-D clip-space transform. Translation is in Vulkan NDC ([-1, 1]^2), after
  // scaling, so for example, under a scale of 3, (-3, -3) would translate to center the lower right
  // corner, whereas (-2, -2) would align the lower right corner with that of the clipping volume.
  // Scaling occurs on the x/y plane. z is unaffected.
  void SetClipSpaceTransform(float x, float y, float scale);
  // Sets the camera pose buffer
  void SetPoseBuffer(const Buffer& buffer, uint32_t num_entries, int64_t base_time,
                     uint64_t time_interval);
  // Overloaded version of |SetPoseBuffer()| using `zx::time` and `zx::duration`.
  void SetPoseBuffer(const Buffer& buffer, uint32_t num_entries, zx::time base_time,
                     zx::duration time_interval);
};

// Represents a camera resource in a session.
class Camera : public CameraBase {
 public:
  explicit Camera(const Scene& scene);
  Camera(Session* session, uint32_t scene_id);
  Camera(Camera&& moved) noexcept;
  ~Camera();

  // Sets the camera's projection parameters.
  void SetProjection(const float fovy);
};

// Represents a StereoCamera resource in a session.
class StereoCamera final : public CameraBase {
 public:
  explicit StereoCamera(const Scene& scene);
  StereoCamera(Session* session, uint32_t scene_id);
  StereoCamera(StereoCamera&& moved) noexcept;
  ~StereoCamera();

  // Sets the camera's projection parameters.
  void SetStereoProjection(const std::array<float, 4 * 4>& left_projection,
                           const std::array<float, 4 * 4>& right_projection);
};

// Represents a renderer resource in a session.
class Renderer final : public Resource {
 public:
  explicit Renderer(Session* session);
  Renderer(Renderer&& moved) noexcept;
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

  // Set whether debug visualization is enabled for this renderer.
  void SetEnableDebugging(bool enable_debugging);
};

// Represents a layer resource in a session.
class Layer final : public Resource {
 public:
  explicit Layer(Session* session);
  Layer(Layer&& moved) noexcept;
  ~Layer();

  // Sets the layer's XY translation and Z-order.
  void SetTranslation(float tx, float ty, float tz) { SetTranslation({tx, ty, tz}); }
  void SetTranslation(const std::array<float, 3>& translation);

  void SetSize(float width, float height) { SetSize({width, height}); }
  void SetSize(const std::array<float, 2>& size);

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
  LayerStack(LayerStack&& moved) noexcept;
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
  DisplayCompositor(DisplayCompositor&& moved) noexcept;
  ~DisplayCompositor();

  // Sets the layer-stack that is to be composited.
  void SetLayerStack(const LayerStack& layer_stack) {
    ZX_DEBUG_ASSERT(session() == layer_stack.session());
    SetLayerStack(layer_stack.id());
  }
  void SetLayerStack(uint32_t layer_stack_id);

  void SetColorConversion(const std::array<float, 3>& preoffsets,
                          const std::array<float, 3 * 3>& matrix,
                          const std::array<float, 3>& postoffsets);

  void SetLayoutRotation(uint32_t rotation_degrees);
};

// Represents a display-less compositor resource in a session.
class Compositor final : public Resource {
 public:
  explicit Compositor(Session* session);
  Compositor(Compositor&& moved) noexcept;
  ~Compositor();

  // Sets the layer-stack that is to be composited.
  void SetLayerStack(const LayerStack& layer_stack) {
    ZX_DEBUG_ASSERT(session() == layer_stack.session());
    SetLayerStack(layer_stack.id());
  }
  void SetLayerStack(uint32_t layer_stack_id);

  void SetLayoutRotation(uint32_t rotation_degrees);
};

}  // namespace scenic

#endif  // LIB_UI_SCENIC_CPP_RESOURCES_H_
