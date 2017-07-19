// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/scene/client/resources.h"

#include "apps/mozart/lib/scene/session_helpers.h"
#include "lib/ftl/logging.h"

namespace mozart {
namespace client {

Resource::Resource(Session* session)
    : session_(session), id_(session->AllocResourceId()) {}

Resource::~Resource() {
  session_->ReleaseResource(id_);
}

void Resource::Export(mx::eventpair export_token) {
  session_->Enqueue(mozart::NewExportResourceOp(id(), std::move(export_token)));
}

void Resource::ExportAsRequest(mx::eventpair* out_import_token) {
  session_->Enqueue(
      mozart::NewExportResourceOpAsRequest(id(), out_import_token));
}

void Resource::SetEventMask(uint32_t event_mask) {
  session_->Enqueue(mozart::NewSetEventMaskOp(id(), event_mask));
}

void Resource::SetLabel(const std::string& label) {
  session_->Enqueue(mozart::NewSetLabelOp(id(), label));
}

Shape::Shape(Session* session) : Resource(session) {}

Shape::~Shape() = default;

Circle::Circle(Session* session, float radius) : Shape(session) {
  session->Enqueue(mozart::NewCreateCircleOp(id(), radius));
}

Circle::~Circle() = default;

Rectangle::Rectangle(Session* session, float width, float height)
    : Shape(session) {
  session->Enqueue(mozart::NewCreateRectangleOp(id(), width, height));
}

Rectangle::~Rectangle() = default;

RoundedRectangle::RoundedRectangle(Session* session,
                                   float width,
                                   float height,
                                   float top_left_radius,
                                   float top_right_radius,
                                   float bottom_right_radius,
                                   float bottom_left_radius)
    : Shape(session) {
  session->Enqueue(mozart::NewCreateRoundedRectangleOp(
      id(), width, height, top_left_radius, top_right_radius,
      bottom_right_radius, bottom_left_radius));
}

RoundedRectangle::~RoundedRectangle() = default;

Image::Image(const Memory& memory,
             off_t memory_offset,
             mozart2::ImageInfoPtr info)
    : Image(memory.session(), memory.id(), memory_offset, std::move(info)) {}

Image::Image(Session* session,
             uint32_t memory_id,
             off_t memory_offset,
             mozart2::ImageInfoPtr info)
    : Resource(session), memory_offset_(memory_offset), info_(*info) {
  session->Enqueue(mozart::NewCreateImageOp(id(), memory_id, memory_offset_,
                                            std::move(info)));
}

Image::~Image() = default;

size_t Image::ComputeSize(const mozart2::ImageInfo& image_info) {
  FTL_DCHECK(image_info.tiling == mozart2::ImageInfo::Tiling::LINEAR);

  switch (image_info.pixel_format) {
    case mozart2::ImageInfo::PixelFormat::BGRA_8:
      return image_info.height * image_info.stride;
  }

  FTL_NOTREACHED();
}

Memory::Memory(Session* session, mx::vmo vmo, mozart2::MemoryType memory_type)
    : Resource(session), memory_type_(memory_type) {
  session->Enqueue(
      mozart::NewCreateMemoryOp(id(), std::move(vmo), memory_type));
}

Memory::~Memory() = default;

Material::Material(Session* session) : Resource(session) {
  session->Enqueue(mozart::NewCreateMaterialOp(id()));
}

Material::~Material() = default;

void Material::SetTexture(uint32_t image_id) {
  session()->Enqueue(mozart::NewSetTextureOp(id(), image_id));
}

void Material::SetColor(uint8_t red,
                        uint8_t green,
                        uint8_t blue,
                        uint8_t alpha) {
  session()->Enqueue(mozart::NewSetColorOp(id(), red, green, blue, alpha));
}

Node::Node(Session* session) : Resource(session) {}

Node::~Node() = default;

void Node::SetTranslation(const float translation[3]) {
  session()->Enqueue(mozart::NewSetTranslationOp(id(), translation));
}

void Node::SetScale(const float scale[3]) {
  session()->Enqueue(mozart::NewSetScaleOp(id(), scale));
}

void Node::SetRotation(const float quaternion[4]) {
  session()->Enqueue(mozart::NewSetRotationOp(id(), quaternion));
}

void Node::SetAnchor(const float anchor[3]) {
  session()->Enqueue(mozart::NewSetAnchorOp(id(), anchor));
}

void Node::SetTag(uint32_t tag_value) {
  session()->Enqueue(mozart::NewSetTagOp(id(), tag_value));
}

void Node::SetHitTestBehavior(mozart2::HitTestBehavior hit_test_behavior) {
  session()->Enqueue(mozart::NewSetHitTestBehaviorOp(id(), hit_test_behavior));
}

void Node::Detach() {
  session()->Enqueue(mozart::NewDetachOp(id()));
}

ShapeNode::ShapeNode(Session* session) : Node(session) {
  session->Enqueue(mozart::NewCreateShapeNodeOp(id()));
}

ShapeNode::~ShapeNode() = default;

void ShapeNode::SetShape(uint32_t shape_id) {
  session()->Enqueue(mozart::NewSetShapeOp(id(), shape_id));
}

void ShapeNode::SetMaterial(uint32_t material_id) {
  session()->Enqueue(mozart::NewSetMaterialOp(id(), material_id));
}

ContainerNode::ContainerNode(Session* session) : Node(session) {}

ContainerNode::~ContainerNode() = default;

void ContainerNode::AddChild(uint32_t child_node_id) {
  session()->Enqueue(mozart::NewAddChildOp(id(), child_node_id));
}

void ContainerNode::AddPart(uint32_t part_node_id) {
  session()->Enqueue(mozart::NewAddPartOp(id(), part_node_id));
}

void ContainerNode::DetachChildren() {
  session()->Enqueue(mozart::NewDetachChildrenOp(id()));
}

EntityNode::EntityNode(Session* session) : ContainerNode(session) {
  session->Enqueue(mozart::NewCreateEntityNodeOp(id()));
}

EntityNode::~EntityNode() = default;

void EntityNode::SetClip(uint32_t clip_id, bool clip_to_self) {
  session()->Enqueue(mozart::NewSetClipOp(id(), clip_id, clip_to_self));
}

ImportNode::ImportNode(Session* session) : ContainerNode(session) {}

ImportNode::~ImportNode() {
  FTL_DCHECK(is_bound_) << "Import was never bound.";
}

void ImportNode::Bind(mx::eventpair import_token) {
  FTL_DCHECK(!is_bound_);
  session()->Enqueue(mozart::NewImportResourceOp(
      id(), mozart2::ImportSpec::NODE, std::move(import_token)));
  is_bound_ = true;
}

void ImportNode::BindAsRequest(mx::eventpair* out_export_token) {
  FTL_DCHECK(!is_bound_);
  session()->Enqueue(mozart::NewImportResourceOpAsRequest(
      id(), mozart2::ImportSpec::NODE, out_export_token));
  is_bound_ = true;
}

ClipNode::ClipNode(Session* session) : ContainerNode(session) {
  session->Enqueue(mozart::NewCreateClipNodeOp(id()));
}

ClipNode::~ClipNode() = default;

OpacityNode::OpacityNode(Session* session) : ContainerNode(session) {
  // TODO(MZ-139): Opacities are not currently implemented. Create an entity
  // node for now.
  session->Enqueue(mozart::NewCreateEntityNodeOp(id()));
}

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
  session->Enqueue(mozart::NewCreateSceneOp(id()));
}

Scene::~Scene() = default;

Camera::Camera(const Scene& scene) : Camera(scene.session(), scene.id()) {}

Camera::Camera(Session* session, uint32_t scene_id) : Resource(session) {
  session->Enqueue(mozart::NewCreateCameraOp(id(), scene_id));
}

Camera::~Camera() = default;

void Camera::SetProjection(const float eye_position[3],
                           const float eye_look_at[3],
                           const float eye_up[3],
                           float fovy) {
  session()->Enqueue(mozart::NewSetCameraProjectionOp(
      id(), eye_position, eye_look_at, eye_up, fovy));
}

DisplayRenderer::DisplayRenderer(Session* session) : Resource(session) {
  session->Enqueue(mozart::NewCreateDisplayRendererOp(id()));
}

DisplayRenderer::~DisplayRenderer() = default;

void DisplayRenderer::SetCamera(uint32_t camera_id) {
  session()->Enqueue(mozart::NewSetCameraOp(id(), camera_id));
}

}  // namespace client
}  // namespace mozart
