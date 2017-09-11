// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/scenic/client/resources.h"

#include "lib/ui/scenic/fidl_helpers.h"
#include "lib/fxl/logging.h"

namespace scenic_lib {

Resource::Resource(Session* session)
    : session_(session), id_(session->AllocResourceId()) {}

Resource::Resource(Resource&& moved)
    : session_(moved.session_), id_(moved.id_) {
  auto& moved_session = *const_cast<Session**>(&moved.session_);
  auto& moved_id = *const_cast<uint32_t*>(&moved.id_);
  moved_session = nullptr;
  moved_id = 0;
}

Resource::~Resource() {
  // If this resource was moved, it is not responsible for releasing the ID.
  if (session_)
    session_->ReleaseResource(id_);
}

void Resource::Export(mx::eventpair export_token) {
  session_->Enqueue(NewExportResourceOp(id(), std::move(export_token)));
}

void Resource::ExportAsRequest(mx::eventpair* out_import_token) {
  session_->Enqueue(NewExportResourceOpAsRequest(id(), out_import_token));
}

void Resource::SetEventMask(uint32_t event_mask) {
  session_->Enqueue(NewSetEventMaskOp(id(), event_mask));
}

void Resource::SetLabel(const std::string& label) {
  session_->Enqueue(NewSetLabelOp(id(), label));
}

Shape::Shape(Session* session) : Resource(session) {}

Shape::Shape(Shape&& moved) : Resource(std::move(moved)) {}

Shape::~Shape() = default;

Circle::Circle(Session* session, float radius) : Shape(session) {
  session->Enqueue(NewCreateCircleOp(id(), radius));
}

Circle::Circle(Circle&& moved) : Shape(std::move(moved)) {}

Circle::~Circle() = default;

Rectangle::Rectangle(Session* session, float width, float height)
    : Shape(session) {
  session->Enqueue(NewCreateRectangleOp(id(), width, height));
}

Rectangle::Rectangle(Rectangle&& moved) : Shape(std::move(moved)) {}

Rectangle::~Rectangle() = default;

RoundedRectangle::RoundedRectangle(Session* session,
                                   float width,
                                   float height,
                                   float top_left_radius,
                                   float top_right_radius,
                                   float bottom_right_radius,
                                   float bottom_left_radius)
    : Shape(session) {
  session->Enqueue(NewCreateRoundedRectangleOp(
      id(), width, height, top_left_radius, top_right_radius,
      bottom_right_radius, bottom_left_radius));
}

RoundedRectangle::RoundedRectangle(RoundedRectangle&& moved)
    : Shape(std::move(moved)) {}

RoundedRectangle::~RoundedRectangle() = default;

Image::Image(const Memory& memory,
             off_t memory_offset,
             scenic::ImageInfoPtr info)
    : Image(memory.session(), memory.id(), memory_offset, std::move(info)) {}

Image::Image(Session* session,
             uint32_t memory_id,
             off_t memory_offset,
             scenic::ImageInfoPtr info)
    : Resource(session), memory_offset_(memory_offset), info_(*info) {
  session->Enqueue(
      NewCreateImageOp(id(), memory_id, memory_offset_, std::move(info)));
}

Image::Image(Image&& moved)
    : Resource(std::move(moved)),
      memory_offset_(moved.memory_offset_),
      info_(moved.info_) {}

Image::~Image() = default;

size_t Image::ComputeSize(const scenic::ImageInfo& image_info) {
  FXL_DCHECK(image_info.tiling == scenic::ImageInfo::Tiling::LINEAR);

  switch (image_info.pixel_format) {
    case scenic::ImageInfo::PixelFormat::BGRA_8:
      return image_info.height * image_info.stride;
  }

  FXL_NOTREACHED();
}

Buffer::Buffer(const Memory& memory, off_t memory_offset, size_t num_bytes)
    : Buffer(memory.session(), memory.id(), memory_offset, num_bytes) {}

Buffer::Buffer(Session* session,
               uint32_t memory_id,
               off_t memory_offset,
               size_t num_bytes)
    : Resource(session) {
  session->Enqueue(
      NewCreateBufferOp(id(), memory_id, memory_offset, num_bytes));
}

Buffer::Buffer(Buffer&& moved) : Resource(std::move(moved)) {}

Buffer::~Buffer() = default;

Memory::Memory(Session* session, mx::vmo vmo, scenic::MemoryType memory_type)
    : Resource(session), memory_type_(memory_type) {
  session->Enqueue(NewCreateMemoryOp(id(), std::move(vmo), memory_type));
}

Memory::Memory(Memory&& moved)
    : Resource(std::move(moved)), memory_type_(moved.memory_type_) {}

Memory::~Memory() = default;

Mesh::Mesh(Session* session) : Shape(session) {
  session->Enqueue(NewCreateMeshOp(id()));
}

Mesh::Mesh(Mesh&& moved) : Shape(std::move(moved)) {}

Mesh::~Mesh() = default;

void Mesh::BindBuffers(const Buffer& index_buffer,
                       scenic::MeshIndexFormat index_format,
                       uint64_t index_offset,
                       uint32_t index_count,
                       const Buffer& vertex_buffer,
                       scenic::MeshVertexFormatPtr vertex_format,
                       uint64_t vertex_offset,
                       uint32_t vertex_count,
                       const float bounding_box_min[3],
                       const float bounding_box_max[3]) {
  session()->Enqueue(NewBindMeshBuffersOp(
      id(), index_buffer.id(), index_format, index_offset, index_count,
      vertex_buffer.id(), std::move(vertex_format), vertex_offset, vertex_count,
      bounding_box_min, bounding_box_max));
}

Material::Material(Session* session) : Resource(session) {
  session->Enqueue(NewCreateMaterialOp(id()));
}

Material::Material(Material&& moved) : Resource(std::move(moved)) {}

Material::~Material() = default;

void Material::SetTexture(uint32_t image_id) {
  session()->Enqueue(NewSetTextureOp(id(), image_id));
}

void Material::SetColor(uint8_t red,
                        uint8_t green,
                        uint8_t blue,
                        uint8_t alpha) {
  session()->Enqueue(NewSetColorOp(id(), red, green, blue, alpha));
}

Node::Node(Session* session) : Resource(session) {}

Node::Node(Node&& moved) : Resource(std::move(moved)) {}

Node::~Node() = default;

void Node::SetTranslation(const float translation[3]) {
  session()->Enqueue(NewSetTranslationOp(id(), translation));
}

void Node::SetScale(const float scale[3]) {
  session()->Enqueue(NewSetScaleOp(id(), scale));
}

void Node::SetRotation(const float quaternion[4]) {
  session()->Enqueue(NewSetRotationOp(id(), quaternion));
}

void Node::SetAnchor(const float anchor[3]) {
  session()->Enqueue(NewSetAnchorOp(id(), anchor));
}

void Node::SetTag(uint32_t tag_value) {
  session()->Enqueue(NewSetTagOp(id(), tag_value));
}

void Node::SetHitTestBehavior(scenic::HitTestBehavior hit_test_behavior) {
  session()->Enqueue(NewSetHitTestBehaviorOp(id(), hit_test_behavior));
}

void Node::Detach() {
  session()->Enqueue(NewDetachOp(id()));
}

ShapeNode::ShapeNode(Session* session) : Node(session) {
  session->Enqueue(NewCreateShapeNodeOp(id()));
}

ShapeNode::ShapeNode(ShapeNode&& moved) : Node(std::move(moved)) {}

ShapeNode::~ShapeNode() = default;

void ShapeNode::SetShape(uint32_t shape_id) {
  session()->Enqueue(NewSetShapeOp(id(), shape_id));
}

void ShapeNode::SetMaterial(uint32_t material_id) {
  session()->Enqueue(NewSetMaterialOp(id(), material_id));
}

ContainerNode::ContainerNode(Session* session) : Node(session) {}

ContainerNode::ContainerNode(ContainerNode&& moved) : Node(std::move(moved)) {}

ContainerNode::~ContainerNode() = default;

void ContainerNode::AddChild(uint32_t child_node_id) {
  session()->Enqueue(NewAddChildOp(id(), child_node_id));
}

void ContainerNode::AddPart(uint32_t part_node_id) {
  session()->Enqueue(NewAddPartOp(id(), part_node_id));
}

void ContainerNode::DetachChildren() {
  session()->Enqueue(NewDetachChildrenOp(id()));
}

EntityNode::EntityNode(Session* session) : ContainerNode(session) {
  session->Enqueue(NewCreateEntityNodeOp(id()));
}

EntityNode::~EntityNode() = default;

void EntityNode::SetClip(uint32_t clip_id, bool clip_to_self) {
  session()->Enqueue(NewSetClipOp(id(), clip_id, clip_to_self));
}

ImportNode::ImportNode(Session* session) : ContainerNode(session) {}

ImportNode::ImportNode(ImportNode&& moved) : ContainerNode(std::move(moved)) {}

ImportNode::~ImportNode() {
  FXL_DCHECK(is_bound_) << "Import was never bound.";
}

void ImportNode::Bind(mx::eventpair import_token) {
  FXL_DCHECK(!is_bound_);
  session()->Enqueue(NewImportResourceOp(id(), scenic::ImportSpec::NODE,
                                         std::move(import_token)));
  is_bound_ = true;
}

void ImportNode::BindAsRequest(mx::eventpair* out_export_token) {
  FXL_DCHECK(!is_bound_);
  session()->Enqueue(NewImportResourceOpAsRequest(
      id(), scenic::ImportSpec::NODE, out_export_token));
  is_bound_ = true;
}

ClipNode::ClipNode(Session* session) : ContainerNode(session) {
  session->Enqueue(NewCreateClipNodeOp(id()));
}

ClipNode::ClipNode(ClipNode&& moved) : ContainerNode(std::move(moved)) {}

ClipNode::~ClipNode() = default;

OpacityNode::OpacityNode(Session* session) : ContainerNode(session) {
  // TODO(MZ-139): Opacities are not currently implemented. Create an entity
  // node for now.
  session->Enqueue(NewCreateEntityNodeOp(id()));
}

OpacityNode::OpacityNode(OpacityNode&& moved)
    : ContainerNode(std::move(moved)) {}

OpacityNode::~OpacityNode() = default;

void OpacityNode::SetOpacity(double opacity) {
  if (opacity < 0.0) {
    opacity = 0.0;
  } else if (opacity > 1.0) {
    opacity = 1.0;
  }
  // TODO(MZ-139): Opacities are not currently implemented.
}

Scene::Scene(Session* session) : ContainerNode(session) {
  session->Enqueue(NewCreateSceneOp(id()));
}

Scene::Scene(Scene&& moved) : ContainerNode(std::move(moved)) {}

Scene::~Scene() = default;

Camera::Camera(const Scene& scene) : Camera(scene.session(), scene.id()) {}

Camera::Camera(Session* session, uint32_t scene_id) : Resource(session) {
  session->Enqueue(NewCreateCameraOp(id(), scene_id));
}

Camera::Camera(Camera&& moved) : Resource(std::move(moved)) {}

Camera::~Camera() = default;

void Camera::SetProjection(const float eye_position[3],
                           const float eye_look_at[3],
                           const float eye_up[3],
                           float fovy) {
  session()->Enqueue(
      NewSetCameraProjectionOp(id(), eye_position, eye_look_at, eye_up, fovy));
}

Renderer::Renderer(Session* session) : Resource(session) {
  session->Enqueue(NewCreateRendererOp(id()));
}

Renderer::Renderer(Renderer&& moved) : Resource(std::move(moved)) {}

Renderer::~Renderer() = default;

void Renderer::SetCamera(uint32_t camera_id) {
  session()->Enqueue(NewSetCameraOp(id(), camera_id));
}

Layer::Layer(Session* session) : Resource(session) {
  session->Enqueue(NewCreateLayerOp(id()));
}

Layer::Layer(Layer&& moved) : Resource(std::move(moved)) {}

Layer::~Layer() = default;

void Layer::SetRenderer(uint32_t renderer_id) {
  session()->Enqueue(NewSetRendererOp(id(), renderer_id));
}

void Layer::SetSize(const float size[2]) {
  session()->Enqueue(NewSetSizeOp(id(), size));
}

LayerStack::LayerStack(Session* session) : Resource(session) {
  session->Enqueue(NewCreateLayerStackOp(id()));
}

LayerStack::LayerStack(LayerStack&& moved) : Resource(std::move(moved)) {}

LayerStack::~LayerStack() = default;

void LayerStack::AddLayer(uint32_t layer_id) {
  session()->Enqueue(NewAddLayerOp(id(), layer_id));
}

DisplayCompositor::DisplayCompositor(Session* session) : Resource(session) {
  session->Enqueue(NewCreateDisplayCompositorOp(id()));
}

DisplayCompositor::DisplayCompositor(DisplayCompositor&& moved)
    : Resource(std::move(moved)) {}

DisplayCompositor::~DisplayCompositor() = default;

void DisplayCompositor::SetLayerStack(uint32_t layer_stack_id) {
  session()->Enqueue(NewSetLayerStackOp(id(), layer_stack_id));
}

}  // namespace scenic_lib
