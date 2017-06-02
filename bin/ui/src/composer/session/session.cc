// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/session/session.h"

#include "apps/mozart/src/composer/print_op.h"
#include "apps/mozart/src/composer/resources/gpu_memory.h"
#include "apps/mozart/src/composer/resources/host_memory.h"
#include "apps/mozart/src/composer/resources/image.h"
#include "apps/mozart/src/composer/resources/nodes/entity_node.h"
#include "apps/mozart/src/composer/resources/nodes/node.h"
#include "apps/mozart/src/composer/resources/nodes/shape_node.h"
#include "apps/mozart/src/composer/resources/nodes/tag_node.h"
#include "apps/mozart/src/composer/resources/shapes/circle_shape.h"
#include "apps/mozart/src/composer/util/unwrap.h"

namespace mozart {
namespace composer {

Session::Session(SessionId id,
                 SessionContext* context,
                 ErrorReporter* error_reporter)
    : id_(id),
      context_(context),
      error_reporter_(error_reporter),
      resources_(error_reporter) {
  FTL_DCHECK(context);
  FTL_DCHECK(error_reporter);
}

Session::~Session() {
  FTL_DCHECK(!is_valid_);
}

bool Session::ApplyOp(const mozart2::OpPtr& op) {
  switch (op->which()) {
    case mozart2::Op::Tag::CREATE_RESOURCE:
      return ApplyCreateResourceOp(op->get_create_resource());
    case mozart2::Op::Tag::RELEASE_RESOURCE:
      return ApplyReleaseResourceOp(op->get_release_resource());
    case mozart2::Op::Tag::ADD_CHILD:
      return ApplyAddChildOp(op->get_add_child());
    case mozart2::Op::Tag::ADD_PART:
      return ApplyAddPartOp(op->get_add_part());
    case mozart2::Op::Tag::DETACH:
      return ApplyDetachOp(op->get_detach());
    case mozart2::Op::Tag::DETACH_CHILDREN:
      return ApplyDetachChildrenOp(op->get_detach_children());
    case mozart2::Op::Tag::SET_TRANSFORM:
      return ApplySetTransformOp(op->get_set_transform());
    case mozart2::Op::Tag::SET_SHAPE:
      return ApplySetShapeOp(op->get_set_shape());
    case mozart2::Op::Tag::SET_MATERIAL:
      return ApplySetMaterialOp(op->get_set_material());
    case mozart2::Op::Tag::SET_CLIP:
      return ApplySetClipOp(op->get_set_clip());
    default:
      error_reporter_->ERROR()
          << "composer::Session::ApplyOp(): unimplemented op: " << op;
      return false;
  }

  return true;
}

bool Session::ApplyCreateResourceOp(const mozart2::CreateResourceOpPtr& op) {
  const ResourceId id = op->id;
  if (id == 0) {
    error_reporter_->ERROR()
        << "composer::Session::ApplyCreateResourceOp(): invalid ID: " << op;
    return false;
  }

  switch (op->resource->which()) {
    case mozart2::Resource::Tag::MEMORY:
      return ApplyCreateMemory(id, op->resource->get_memory());
    case mozart2::Resource::Tag::IMAGE:
      return ApplyCreateImage(id, op->resource->get_image());
    case mozart2::Resource::Tag::BUFFER:
      return ApplyCreateBuffer(id, op->resource->get_buffer());
    case mozart2::Resource::Tag::LINK:
      return ApplyCreateLink(id, op->resource->get_link());
    case mozart2::Resource::Tag::RECTANGLE:
      return ApplyCreateRectangle(id, op->resource->get_rectangle());
    case mozart2::Resource::Tag::CIRCLE:
      return ApplyCreateCircle(id, op->resource->get_circle());
    case mozart2::Resource::Tag::MESH:
      return ApplyCreateMesh(id, op->resource->get_mesh());
    case mozart2::Resource::Tag::MATERIAL:
      return ApplyCreateMaterial(id, op->resource->get_material());
    case mozart2::Resource::Tag::CLIP_NODE:
      return ApplyCreateClipNode(id, op->resource->get_clip_node());
    case mozart2::Resource::Tag::ENTITY_NODE:
      return ApplyCreateEntityNode(id, op->resource->get_entity_node());
    case mozart2::Resource::Tag::LINK_NODE:
      return ApplyCreateLinkNode(id, op->resource->get_link_node());
    case mozart2::Resource::Tag::SHAPE_NODE:
      return ApplyCreateShapeNode(id, op->resource->get_shape_node());
    case mozart2::Resource::Tag::TAG_NODE:
      return ApplyCreateTagNode(id, op->resource->get_tag_node());
    default:
      error_reporter_->ERROR()
          << "composer::Session::ApplyCreateResourceOp(): unsupported resource"
          << op;
      return false;
  }
}

bool Session::ApplyReleaseResourceOp(const mozart2::ReleaseResourceOpPtr& op) {
  return resources_.RemoveResource(op->id);
}

bool Session::ApplyAddChildOp(const mozart2::AddChildOpPtr& op) {
  // Find the parent and child nodes.
  if (auto parent_node = resources_.FindResource<Node>(op->node_id)) {
    if (auto child_node = resources_.FindResource<Node>(op->child_id)) {
      return parent_node->AddChild(std::move(child_node));
    }
  }
  return false;
}

bool Session::ApplyAddPartOp(const mozart2::AddPartOpPtr& op) {
  // Find the parent and part nodes.
  if (auto parent_node = resources_.FindResource<Node>(op->node_id)) {
    if (auto part_node = resources_.FindResource<Node>(op->part_id)) {
      return parent_node->AddPart(std::move(part_node));
    }
  }
  return false;
}

bool Session::ApplyDetachOp(const mozart2::DetachOpPtr& op) {
  if (auto node = resources_.FindResource<Node>(op->node_id)) {
    return Node::Detach(node);
  }
  return false;
}

bool Session::ApplyDetachChildrenOp(const mozart2::DetachChildrenOpPtr& op) {
  error_reporter_->ERROR()
      << "composer::Session::ApplyDetachChildrenOp(): unimplemented";
  return false;
}

bool Session::ApplySetTransformOp(const mozart2::SetTransformOpPtr& op) {
  if (auto node = resources_.FindResource<Node>(op->node_id)) {
    node->SetTransform(Unwrap(op->transform));
    return true;
  }
  return false;
}

bool Session::ApplySetShapeOp(const mozart2::SetShapeOpPtr& op) {
  if (auto node = resources_.FindResource<ShapeNode>(op->node_id)) {
    if (auto shape = resources_.FindResource<Shape>(op->shape_id)) {
      node->SetShape(std::move(shape));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetMaterialOp(const mozart2::SetMaterialOpPtr& op) {
  if (auto node = resources_.FindResource<ShapeNode>(op->node_id)) {
    if (auto material = resources_.FindResource<Material>(op->material_id)) {
      node->SetMaterial(std::move(material));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetClipOp(const mozart2::SetClipOpPtr& op) {
  error_reporter_->ERROR()
      << "composer::Session::ApplySetClipOp(): unimplemented";
  return false;
}

bool Session::ApplyCreateMemory(ResourceId id, const mozart2::MemoryPtr& args) {
  auto memory = CreateMemory(id, args);
  return memory ? resources_.AddResource(id, std::move(memory)) : false;
}

bool Session::ApplyCreateImage(ResourceId id, const mozart2::ImagePtr& args) {
  if (auto memory = resources_.FindResource<Memory>(args->memory_id)) {
    if (auto image = CreateImage(id, memory, args)) {
      return resources_.AddResource(id, std::move(image));
    }
  }

  return false;
}

bool Session::ApplyCreateBuffer(ResourceId id, const mozart2::BufferPtr& args) {
  error_reporter_->ERROR()
      << "composer::Session::ApplyCreateBuffer(): unimplemented";
  return false;
}

bool Session::ApplyCreateLink(ResourceId id, const mozart2::LinkPtr& args) {
  auto link = context_->CreateLink(this, id, args);
  return link ? resources_.AddResource(id, std::move(link)) : false;
}

bool Session::ApplyCreateRectangle(ResourceId id,
                                   const mozart2::RectanglePtr& args) {
  error_reporter_->ERROR()
      << "composer::Session::ApplyCreateRectangle(): unimplemented";
  return false;
}

bool Session::ApplyCreateCircle(ResourceId id, const mozart2::CirclePtr& args) {
  auto tag = args->radius->which();
  if (tag == mozart2::Value::Tag::VECTOR1) {
    float initial_radius = args->radius->get_vector1();
    auto circle = CreateCircle(id, initial_radius);
    return circle ? resources_.AddResource(id, std::move(circle)) : false;
  } else if (tag == mozart2::Value::Tag::VARIABLE_ID) {
    error_reporter_->ERROR() << "composer::Session::ApplyCreateCircle(): "
                                "unimplemented: variable radius";
    return false;
  } else {
    error_reporter_->ERROR() << "composer::Session::ApplyCreateCircle(): "
                                "radius must be a float or a variable of type "
                                "float";
    return false;
  }
}

bool Session::ApplyCreateMesh(ResourceId id, const mozart2::MeshPtr& args) {
  error_reporter_->ERROR()
      << "composer::Session::ApplyCreateMesh(): unimplemented";
  return false;
}

bool Session::ApplyCreateMaterial(ResourceId id,
                                  const mozart2::MaterialPtr& args) {
  float red = 1.f;
  float green = 1.f;
  float blue = 1.f;
  float alpha = 1.f;
  if (args->color) {
    auto& color = args->color;
    red = static_cast<float>(color->red) / 255.f;
    green = static_cast<float>(color->green) / 255.f;
    blue = static_cast<float>(color->blue) / 255.f;
    alpha = static_cast<float>(color->alpha) / 255.f;
  }
  ImagePtr image;
  if (args->texture_id != 0) {
    image = resources_.FindResource<Image>(args->texture_id);
    if (!image) {
      return false;
    }
  }
  auto material = CreateMaterial(id, image, red, green, blue, alpha);
  return material ? resources_.AddResource(id, std::move(material)) : false;
}

bool Session::ApplyCreateClipNode(ResourceId id,
                                  const mozart2::ClipNodePtr& args) {
  auto node = CreateClipNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateEntityNode(ResourceId id,
                                    const mozart2::EntityNodePtr& args) {
  auto node = CreateEntityNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateLinkNode(ResourceId id,
                                  const mozart2::LinkNodePtr& args) {
  auto node = CreateLinkNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateShapeNode(ResourceId id,
                                   const mozart2::ShapeNodePtr& args) {
  auto node = CreateShapeNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateTagNode(ResourceId id,
                                 const mozart2::TagNodePtr& args) {
  auto node = CreateTagNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

ResourcePtr Session::CreateMemory(ResourceId, const mozart2::MemoryPtr& args) {
  vk::Device device = context()->vk_device();
  switch (args->memory_type) {
    case mozart2::Memory::MemoryType::VK_DEVICE_MEMORY:
      return GpuMemory::New(this, device, args, error_reporter_);
    case mozart2::Memory::MemoryType::HOST_MEMORY:
      return HostMemory::New(this, device, args, error_reporter_);
  }
}

ResourcePtr Session::CreateImage(ResourceId,
                                 MemoryPtr memory,
                                 const mozart2::ImagePtr& args) {
  return Image::New(this, memory, args, error_reporter_);
}

ResourcePtr Session::CreateLink(ResourceId id, const mozart2::LinkPtr& args) {
  return context()->CreateLink(this, id, args);
}

ResourcePtr Session::CreateClipNode(ResourceId id,
                                    const mozart2::ClipNodePtr& args) {
  error_reporter_->ERROR() << "composer::Session::CreateClipNode(): "
                              "unimplemented.";
  return ResourcePtr();
}

ResourcePtr Session::CreateEntityNode(ResourceId id,
                                      const mozart2::EntityNodePtr& args) {
  return ftl::MakeRefCounted<EntityNode>(this, id);
}

ResourcePtr Session::CreateLinkNode(ResourceId id,
                                    const mozart2::LinkNodePtr& args) {
  error_reporter_->ERROR() << "composer::Session::CreateLinkNode(): "
                              "unimplemented.";
  return ResourcePtr();
}

ResourcePtr Session::CreateShapeNode(ResourceId id,
                                     const mozart2::ShapeNodePtr& args) {
  return ftl::MakeRefCounted<ShapeNode>(this, id);
}

ResourcePtr Session::CreateTagNode(ResourceId id,
                                   const mozart2::TagNodePtr& args) {
  return ftl::MakeRefCounted<TagNode>(this, id, args->tag_value);
}

ResourcePtr Session::CreateCircle(ResourceId id, float initial_radius) {
  return ftl::MakeRefCounted<CircleShape>(this, initial_radius);
}

ResourcePtr Session::CreateMaterial(ResourceId id,
                                    ImagePtr image,
                                    float red,
                                    float green,
                                    float blue,
                                    float alpha) {
  return ftl::MakeRefCounted<Material>(this, red, green, blue, alpha, image);
}

void Session::TearDown() {
  if (!is_valid_) {
    // TearDown already called.
    return;
  }
  is_valid_ = false;
  error_reporter_ = nullptr;
  resources_.Clear();

  context()->OnSessionTearDown(this);
  FTL_DCHECK(resource_count_ == 0);
}

ErrorReporter* Session::error_reporter() const {
  return error_reporter_ ? error_reporter_ : ErrorReporter::Default();
}

}  // namespace composer
}  // namespace mozart
