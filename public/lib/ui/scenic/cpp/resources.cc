// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/scenic/cpp/resources.h"

#include <fbl/algorithm.h>

#include "lib/fxl/logging.h"
#include "lib/images/images_util.h"
#include "lib/ui/scenic/cpp/fidl_helpers.h"

namespace scenic {

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

void Resource::Export(zx::eventpair export_token) {
  session_->Enqueue(NewExportResourceCmd(id(), std::move(export_token)));
}

void Resource::ExportAsRequest(zx::eventpair* out_import_token) {
  session_->Enqueue(NewExportResourceCmdAsRequest(id(), out_import_token));
}

void Resource::SetEventMask(uint32_t event_mask) {
  session_->Enqueue(NewSetEventMaskCmd(id(), event_mask));
}

void Resource::SetLabel(const std::string& label) {
  session_->Enqueue(NewSetLabelCmd(id(), label));
}

Shape::Shape(Session* session) : Resource(session) {}

Shape::Shape(Shape&& moved) : Resource(std::move(moved)) {}

Shape::~Shape() = default;

Circle::Circle(Session* session, float radius) : Shape(session) {
  session->Enqueue(NewCreateCircleCmd(id(), radius));
}

Circle::Circle(Circle&& moved) : Shape(std::move(moved)) {}

Circle::~Circle() = default;

Rectangle::Rectangle(Session* session, float width, float height)
    : Shape(session) {
  session->Enqueue(NewCreateRectangleCmd(id(), width, height));
}

Rectangle::Rectangle(Rectangle&& moved) : Shape(std::move(moved)) {}

Rectangle::~Rectangle() = default;

RoundedRectangle::RoundedRectangle(Session* session, float width, float height,
                                   float top_left_radius,
                                   float top_right_radius,
                                   float bottom_right_radius,
                                   float bottom_left_radius)
    : Shape(session) {
  session->Enqueue(NewCreateRoundedRectangleCmd(
      id(), width, height, top_left_radius, top_right_radius,
      bottom_right_radius, bottom_left_radius));
}

RoundedRectangle::RoundedRectangle(RoundedRectangle&& moved)
    : Shape(std::move(moved)) {}

RoundedRectangle::~RoundedRectangle() = default;

Image::Image(const Memory& memory, off_t memory_offset,
             fuchsia::images::ImageInfo info)
    : Image(memory.session(), memory.id(), memory_offset, std::move(info)) {}

Image::Image(Session* session, uint32_t memory_id, off_t memory_offset,
             fuchsia::images::ImageInfo info)
    : Resource(session), memory_offset_(memory_offset), info_(info) {
  session->Enqueue(
      NewCreateImageCmd(id(), memory_id, memory_offset_, std::move(info)));
}

Image::Image(Image&& moved)
    : Resource(std::move(moved)),
      memory_offset_(moved.memory_offset_),
      info_(moved.info_) {}

Image::~Image() = default;

size_t Image::ComputeSize(const fuchsia::images::ImageInfo& image_info) {
  return images_util::ImageSize(image_info);
}

Buffer::Buffer(const Memory& memory, off_t memory_offset, size_t num_bytes)
    : Buffer(memory.session(), memory.id(), memory_offset, num_bytes) {}

Buffer::Buffer(Session* session, uint32_t memory_id, off_t memory_offset,
               size_t num_bytes)
    : Resource(session) {
  session->Enqueue(
      NewCreateBufferCmd(id(), memory_id, memory_offset, num_bytes));
}

Buffer::Buffer(Buffer&& moved) : Resource(std::move(moved)) {}

Buffer::~Buffer() = default;

Memory::Memory(Session* session, zx::vmo vmo,
               fuchsia::images::MemoryType memory_type)
    : Resource(session), memory_type_(memory_type) {
  session->Enqueue(NewCreateMemoryCmd(id(), std::move(vmo), memory_type));
}

Memory::Memory(Memory&& moved)
    : Resource(std::move(moved)), memory_type_(moved.memory_type_) {}

Memory::~Memory() = default;

Mesh::Mesh(Session* session) : Shape(session) {
  session->Enqueue(NewCreateMeshCmd(id()));
}

Mesh::Mesh(Mesh&& moved) : Shape(std::move(moved)) {}

Mesh::~Mesh() = default;

void Mesh::BindBuffers(const Buffer& index_buffer,
                       fuchsia::ui::gfx::MeshIndexFormat index_format,
                       uint64_t index_offset, uint32_t index_count,
                       const Buffer& vertex_buffer,
                       fuchsia::ui::gfx::MeshVertexFormat vertex_format,
                       uint64_t vertex_offset, uint32_t vertex_count,
                       const float bounding_box_min[3],
                       const float bounding_box_max[3]) {
  FXL_DCHECK(session() == index_buffer.session() &&
             session() == vertex_buffer.session());
  session()->Enqueue(NewBindMeshBuffersCmd(
      id(), index_buffer.id(), index_format, index_offset, index_count,
      vertex_buffer.id(), std::move(vertex_format), vertex_offset, vertex_count,
      bounding_box_min, bounding_box_max));
}

Material::Material(Session* session) : Resource(session) {
  session->Enqueue(NewCreateMaterialCmd(id()));
}

Material::Material(Material&& moved) : Resource(std::move(moved)) {}

Material::~Material() = default;

void Material::SetTexture(uint32_t image_id) {
  session()->Enqueue(NewSetTextureCmd(id(), image_id));
}

void Material::SetColor(uint8_t red, uint8_t green, uint8_t blue,
                        uint8_t alpha) {
  session()->Enqueue(NewSetColorCmd(id(), red, green, blue, alpha));
}

Node::Node(Session* session) : Resource(session) {}

Node::Node(Node&& moved) : Resource(std::move(moved)) {}

Node::~Node() = default;

void Node::SetTranslation(const float translation[3]) {
  session()->Enqueue(NewSetTranslationCmd(id(), translation));
}

void Node::SetTranslation(uint32_t variable_id) {
  session()->Enqueue(NewSetTranslationCmd(id(), variable_id));
}

void Node::SetScale(const float scale[3]) {
  session()->Enqueue(NewSetScaleCmd(id(), scale));
}

void Node::SetScale(uint32_t variable_id) {
  session()->Enqueue(NewSetScaleCmd(id(), variable_id));
}

void Node::SetRotation(const float quaternion[4]) {
  session()->Enqueue(NewSetRotationCmd(id(), quaternion));
}

void Node::SetRotation(uint32_t variable_id) {
  session()->Enqueue(NewSetRotationCmd(id(), variable_id));
}

void Node::SetAnchor(const float anchor[3]) {
  session()->Enqueue(NewSetAnchorCmd(id(), anchor));
}

void Node::SetAnchor(uint32_t variable_id) {
  session()->Enqueue(NewSetAnchorCmd(id(), variable_id));
}

void Node::SetTag(uint32_t tag_value) {
  session()->Enqueue(NewSetTagCmd(id(), tag_value));
}

void Node::SetHitTestBehavior(
    fuchsia::ui::gfx::HitTestBehavior hit_test_behavior) {
  session()->Enqueue(NewSetHitTestBehaviorCmd(id(), hit_test_behavior));
}

void Node::Detach() { session()->Enqueue(NewDetachCmd(id())); }

ShapeNode::ShapeNode(Session* session) : Node(session) {
  session->Enqueue(NewCreateShapeNodeCmd(id()));
}

ShapeNode::ShapeNode(ShapeNode&& moved) : Node(std::move(moved)) {}

ShapeNode::~ShapeNode() = default;

void ShapeNode::SetShape(uint32_t shape_id) {
  session()->Enqueue(NewSetShapeCmd(id(), shape_id));
}

void ShapeNode::SetMaterial(uint32_t material_id) {
  session()->Enqueue(NewSetMaterialCmd(id(), material_id));
}

ContainerNode::ContainerNode(Session* session) : Node(session) {}

ContainerNode::ContainerNode(ContainerNode&& moved) : Node(std::move(moved)) {}

ContainerNode::~ContainerNode() = default;

void ContainerNode::AddChild(uint32_t child_node_id) {
  session()->Enqueue(NewAddChildCmd(id(), child_node_id));
}

void ContainerNode::AddPart(uint32_t part_node_id) {
  session()->Enqueue(NewAddPartCmd(id(), part_node_id));
}

void ContainerNode::DetachChildren() {
  session()->Enqueue(NewDetachChildrenCmd(id()));
}

EntityNode::EntityNode(Session* session) : ContainerNode(session) {
  session->Enqueue(NewCreateEntityNodeCmd(id()));
}

void EntityNode::Attach(const ViewHolder& view_holder) {
  session()->Enqueue(NewAddChildCmd(id(), view_holder.id()));
}

EntityNode::~EntityNode() = default;

void EntityNode::SetClip(uint32_t clip_id, bool clip_to_self) {
  session()->Enqueue(NewSetClipCmd(id(), clip_id, clip_to_self));
}

ImportNode::ImportNode(Session* session) : ContainerNode(session) {}

ImportNode::ImportNode(ImportNode&& moved) : ContainerNode(std::move(moved)) {}

ImportNode::~ImportNode() {
  FXL_DCHECK(is_bound_) << "Import was never bound.";
}

void ImportNode::Bind(zx::eventpair import_token) {
  FXL_DCHECK(!is_bound_);
  session()->Enqueue(NewImportResourceCmd(
      id(), fuchsia::ui::gfx::ImportSpec::NODE, std::move(import_token)));
  is_bound_ = true;
}

void ImportNode::BindAsRequest(zx::eventpair* out_export_token) {
  FXL_DCHECK(!is_bound_);
  session()->Enqueue(NewImportResourceCmdAsRequest(
      id(), fuchsia::ui::gfx::ImportSpec::NODE, out_export_token));
  is_bound_ = true;
}

ViewHolder::ViewHolder(Session* session, zx::eventpair token,
                       const std::string& debug_name)
    : Resource(session) {
  session->Enqueue(NewCreateViewHolderCmd(id(), std::move(token), debug_name));
}

ViewHolder::~ViewHolder() = default;

void ViewHolder::SetViewProperties(const float bounding_box_min[3],
                                   const float bounding_box_max[3],
                                   const float inset_from_min[3],
                                   const float inset_from_max[3]) {
  session()->Enqueue(NewSetViewPropertiesCmd(id(), bounding_box_min,
                                             bounding_box_max, inset_from_min,
                                             inset_from_max));
}

void ViewHolder::SetViewProperties(
    const fuchsia::ui::gfx::ViewProperties& props) {
  session()->Enqueue(NewSetViewPropertiesCmd(id(), props));
}

View::View(Session* session, zx::eventpair token, const std::string& debug_name)
    : Resource(session) {
  session->Enqueue(NewCreateViewCmd(id(), std::move(token), debug_name));
}

View::~View() = default;

void View::AddChild(const Node& child) const {
  FXL_DCHECK(session() == child.session());
  session()->Enqueue(NewAddChildCmd(id(), child.id()));
}

ClipNode::ClipNode(Session* session) : ContainerNode(session) {
  session->Enqueue(NewCreateClipNodeCmd(id()));
}

ClipNode::ClipNode(ClipNode&& moved) : ContainerNode(std::move(moved)) {}

ClipNode::~ClipNode() = default;

OpacityNode::OpacityNode(Session* session) : ContainerNode(session) {
  session->Enqueue(NewCreateOpacityNodeCmd(id()));
}

OpacityNode::OpacityNode(OpacityNode&& moved)
    : ContainerNode(std::move(moved)) {}

OpacityNode::~OpacityNode() = default;

void OpacityNode::SetOpacity(float opacity) {
  opacity = fbl::clamp(opacity, 0.f, 1.f);
  session()->Enqueue(NewSetOpacityCmd(id(), opacity));
}

Variable::Variable(Session* session, fuchsia::ui::gfx::Value initial_value)
    : Resource(session) {
  session->Enqueue(NewCreateVariableCmd(id(), std::move(initial_value)));
}

Variable::Variable(Variable&& moved) : Resource(std::move(moved)) {}

Variable::~Variable() = default;

Scene::Scene(Session* session) : ContainerNode(session) {
  session->Enqueue(NewCreateSceneCmd(id()));
}

Scene::Scene(Scene&& moved) : ContainerNode(std::move(moved)) {}

Scene::~Scene() = default;

void Scene::AddLight(uint32_t light_id) {
  session()->Enqueue(NewAddLightCmd(id(), light_id));
}

void Scene::DetachLights() { session()->Enqueue(NewDetachLightsCmd(id())); }

void CameraBase::SetTransform(const float eye_position[3],
                              const float eye_look_at[3],
                              const float eye_up[3]) {
  session()->Enqueue(
      NewSetCameraTransformCmd(id(), eye_position, eye_look_at, eye_up));
}

void CameraBase::SetPoseBuffer(const Buffer& buffer, uint32_t num_entries,
                               uint64_t base_time, uint64_t time_interval) {
  session()->Enqueue(NewSetCameraPoseBufferCmd(id(), buffer.id(), num_entries,
                                               base_time, time_interval));
}

Camera::Camera(const Scene& scene) : Camera(scene.session(), scene.id()) {}

Camera::Camera(Session* session, uint32_t scene_id) : CameraBase(session) {
  session->Enqueue(NewCreateCameraCmd(id(), scene_id));
}

Camera::Camera(Camera&& moved) : CameraBase(std::move(moved)) {}

Camera::~Camera() = default;

void Camera::SetProjection(const float fovy) {
  session()->Enqueue(NewSetCameraProjectionCmd(id(), fovy));
}

StereoCamera::StereoCamera(const Scene& scene)
    : StereoCamera(scene.session(), scene.id()) {}

StereoCamera::StereoCamera(Session* session, uint32_t scene_id)
    : CameraBase(session) {
  session->Enqueue(NewCreateStereoCameraCmd(id(), scene_id));
}

StereoCamera::StereoCamera(StereoCamera&& moved)
    : CameraBase(std::move(moved)) {}

StereoCamera::~StereoCamera() = default;

void StereoCamera::SetStereoProjection(const float left_projection[16],
                                       const float right_projection[16]) {
  session()->Enqueue(
      NewSetStereoCameraProjectionCmd(id(), left_projection, right_projection));
}

Renderer::Renderer(Session* session) : Resource(session) {
  session->Enqueue(NewCreateRendererCmd(id()));
}

Renderer::Renderer(Renderer&& moved) : Resource(std::move(moved)) {}

Renderer::~Renderer() = default;

void Renderer::SetCamera(uint32_t camera_id) {
  session()->Enqueue(NewSetCameraCmd(id(), camera_id));
}

void Renderer::SetParam(fuchsia::ui::gfx::RendererParam param) {
  session()->Enqueue(NewSetRendererParamCmd(id(), std::move(param)));
}

void Renderer::SetShadowTechnique(fuchsia::ui::gfx::ShadowTechnique technique) {
  auto param = fuchsia::ui::gfx::RendererParam();
  param.set_shadow_technique(technique);
  SetParam(std::move(param));
}

void Renderer::SetDisableClipping(bool disable_clipping) {
  session()->Enqueue(NewSetDisableClippingCmd(id(), disable_clipping));
}

Layer::Layer(Session* session) : Resource(session) {
  session->Enqueue(NewCreateLayerCmd(id()));
}

Layer::Layer(Layer&& moved) : Resource(std::move(moved)) {}

Layer::~Layer() = default;

void Layer::SetRenderer(uint32_t renderer_id) {
  session()->Enqueue(NewSetRendererCmd(id(), renderer_id));
}

void Layer::SetSize(const float size[2]) {
  session()->Enqueue(NewSetSizeCmd(id(), size));
}

LayerStack::LayerStack(Session* session) : Resource(session) {
  session->Enqueue(NewCreateLayerStackCmd(id()));
}

LayerStack::LayerStack(LayerStack&& moved) : Resource(std::move(moved)) {}

LayerStack::~LayerStack() = default;

void LayerStack::AddLayer(uint32_t layer_id) {
  session()->Enqueue(NewAddLayerCmd(id(), layer_id));
}

void LayerStack::RemoveLayer(uint32_t layer_id) {
  session()->Enqueue(NewRemoveLayerCmd(id(), layer_id));
}

void LayerStack::RemoveAllLayers() {
  session()->Enqueue(NewRemoveAllLayersCmd(id()));
}

DisplayCompositor::DisplayCompositor(Session* session) : Resource(session) {
  session->Enqueue(NewCreateDisplayCompositorCmd(id()));
}

DisplayCompositor::DisplayCompositor(DisplayCompositor&& moved)
    : Resource(std::move(moved)) {}

DisplayCompositor::~DisplayCompositor() = default;

void DisplayCompositor::SetLayerStack(uint32_t layer_stack_id) {
  session()->Enqueue(NewSetLayerStackCmd(id(), layer_stack_id));
}

Light::Light(Session* session) : Resource(session) {}

Light::Light(Light&& moved) : Resource(std::move(moved)) {}

Light::~Light() = default;

void Light::SetColor(const float rgb[3]) {
  session()->Enqueue(NewSetLightColorCmd(id(), rgb));
}

void Light::SetColor(uint32_t variable_id) {
  session()->Enqueue(NewSetLightColorCmd(id(), variable_id));
}

void Light::Detach() { session()->Enqueue(NewDetachLightCmd(id())); }

AmbientLight::AmbientLight(Session* session) : Light(session) {
  session->Enqueue(NewCreateAmbientLightCmd(id()));
}

AmbientLight::AmbientLight(AmbientLight&& moved) : Light(std::move(moved)) {}

AmbientLight::~AmbientLight() = default;

DirectionalLight::DirectionalLight(Session* session) : Light(session) {
  session->Enqueue(NewCreateDirectionalLightCmd(id()));
}

DirectionalLight::DirectionalLight(DirectionalLight&& moved)
    : Light(std::move(moved)) {}

DirectionalLight::~DirectionalLight() = default;

void DirectionalLight::SetDirection(const float direction[3]) {
  session()->Enqueue(NewSetLightDirectionCmd(id(), direction));
}

void DirectionalLight::SetDirection(uint32_t variable_id) {
  session()->Enqueue(NewSetLightDirectionCmd(id(), variable_id));
}

}  // namespace scenic
