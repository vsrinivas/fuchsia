// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_SNAPSHOT_SERIALIZER_H_
#define GARNET_LIB_UI_GFX_RESOURCES_SNAPSHOT_SERIALIZER_H_

#include "garnet/lib/ui/gfx/resources/snapshot/snapshot_generated.h"
#include "src/ui/lib/escher/vk/buffer.h"

namespace scenic_impl {
namespace gfx {

using namespace flatbuffers;

// The set of |Serializer| classes in this file are used to save the scenic
// node graph into a flatbuffer representation. They help capture the
// hierarchical representation of the scene graph, which is needed for
// constructing the flatbuffer representation. Flatbuffers are constructed
// inside out, from the leaf node to the root node. This is unwieldly to do
// during tree-traversal using |ResourceVisitor|. Hence a need to recreate
// the hierarchy in these set of classes.

// Defines a template that serializes to flatbuffer builder.
template <typename T>
class Serializer {
 public:
  virtual ~Serializer() = default;

  virtual Offset<T> serialize(FlatBufferBuilder& builder) = 0;
};

class ShapeSerializer : public Serializer<void> {
 public:
  virtual snapshot::Shape type() = 0;
};

class MeshSerializer : public ShapeSerializer {
 public:
  virtual snapshot::Shape type() override { return snapshot::Shape_Mesh; }
  virtual Offset<void> serialize(FlatBufferBuilder& builder) override {
    return snapshot::CreateMesh(builder).Union();
  }
};

class CircleSerializer : public ShapeSerializer {
 public:
  float radius;

  virtual snapshot::Shape type() override { return snapshot::Shape_Circle; }
  virtual Offset<void> serialize(FlatBufferBuilder& builder) override {
    return snapshot::CreateCircle(builder, radius).Union();
  }
};

class RectangleSerializer : public ShapeSerializer {
 public:
  float width, height;

  virtual snapshot::Shape type() override { return snapshot::Shape_Rectangle; }
  virtual Offset<void> serialize(FlatBufferBuilder& builder) override {
    return snapshot::CreateRectangle(builder, width, height).Union();
  }
};

class RoundedRectangleSerializer : public ShapeSerializer {
 public:
  float width, height;

  float top_left_radius;
  float top_right_radius;
  float bottom_right_radius;
  float bottom_left_radius;

  virtual snapshot::Shape type() override {
    return snapshot::Shape_RoundedRectangle;
  }
  virtual Offset<void> serialize(FlatBufferBuilder& builder) override {
    return snapshot::CreateRoundedRectangle(
               builder, width, height, top_left_radius, top_right_radius,
               bottom_right_radius, bottom_left_radius)
        .Union();
  }
};

class AttributeBufferSerializer : public Serializer<snapshot::AttributeBuffer> {
 public:
  int vertex_count;
  int stride;
  escher::BufferPtr buffer;

  virtual Offset<snapshot::AttributeBuffer> serialize(
      FlatBufferBuilder& builder) override {
    uint8_t* bytes = nullptr;
    auto fb_buffer = builder.CreateUninitializedVector(buffer->size(), &bytes);
    memcpy(bytes, buffer->host_ptr(), buffer->size());

    return snapshot::CreateAttributeBuffer(builder, fb_buffer, vertex_count,
                                           stride);
  }
};

class IndexBufferSerializer : public Serializer<snapshot::IndexBuffer> {
 public:
  int index_count;
  escher::BufferPtr buffer;

  virtual Offset<snapshot::IndexBuffer> serialize(
      FlatBufferBuilder& builder) override {
    uint8_t* bytes = nullptr;
    auto fb_buffer = builder.CreateUninitializedVector(buffer->size(), &bytes);
    memcpy(bytes, buffer->host_ptr(), buffer->size());

    return snapshot::CreateIndexBuffer(builder, fb_buffer, index_count);
  }
};

class GeometrySerializer : public Serializer<snapshot::Geometry> {
 public:
  std::vector<std::shared_ptr<AttributeBufferSerializer>> attributes;
  std::shared_ptr<IndexBufferSerializer> indices;
  snapshot::Vec3 bbox_min;
  snapshot::Vec3 bbox_max;

  virtual Offset<snapshot::Geometry> serialize(
      FlatBufferBuilder& builder) override {
    auto fb_indices = indices->serialize(builder);
    std::vector<Offset<snapshot::AttributeBuffer>> attr_vector;
    for (auto& attribute : attributes) {
      attr_vector.push_back(attribute->serialize(builder));
    }
    auto fb_attributes = builder.CreateVector(attr_vector);

    return snapshot::CreateGeometry(builder, fb_attributes, fb_indices,
                                    &bbox_min, &bbox_max);
  }
};

class MaterialSerializer : public Serializer<void> {
 public:
  virtual snapshot::Material type() = 0;
};

class ColorSerializer : public MaterialSerializer {
 public:
  float red, green, blue, alpha;

  virtual snapshot::Material type() override {
    return snapshot::Material_Color;
  }
  virtual Offset<void> serialize(FlatBufferBuilder& builder) override {
    return snapshot::CreateColor(builder, red, green, blue, alpha).Union();
  }
};

class ImageSerializer : public MaterialSerializer {
 public:
  int32_t format, width, height;
  escher::BufferPtr buffer;

  virtual snapshot::Material type() override {
    return snapshot::Material_Image;
  }
  virtual Offset<void> serialize(FlatBufferBuilder& builder) override {
    uint8_t* bytes = nullptr;
    auto data = builder.CreateUninitializedVector(buffer->size(), &bytes);
    memcpy(bytes, buffer->host_ptr(), buffer->size());

    return snapshot::CreateImage(builder, format, width, height, data).Union();
  }
};

class TransformSerializer : public Serializer<snapshot::Transform> {
 public:
  snapshot::Vec3 translation = {0.0, 0.0, 0.0};
  snapshot::Vec3 scale = {1.0, 1.0, 1.0};
  snapshot::Quat rotation = {0.0, 0.0, 0.0, 1.0};
  snapshot::Vec3 anchor = {0.0, 0.0, 0.0};

  virtual Offset<snapshot::Transform> serialize(
      FlatBufferBuilder& builder) override {
    return snapshot::CreateTransform(builder, &translation, &scale, &rotation,
                                     &anchor);
  }
};

class NodeSerializer : public Serializer<snapshot::Node> {
 public:
  std::string name;
  std::shared_ptr<TransformSerializer> transform;

  std::shared_ptr<ShapeSerializer> shape;
  std::shared_ptr<GeometrySerializer> mesh;
  std::shared_ptr<MaterialSerializer> material;

  std::vector<std::shared_ptr<NodeSerializer>> children;

  virtual Offset<snapshot::Node> serialize(
      FlatBufferBuilder& builder) override {
    auto fb_name = name.length() ? builder.CreateString(name) : 0;
    auto fb_transform = transform ? transform->serialize(builder) : 0;

    auto fb_shape_type = shape ? shape->type() : snapshot::Shape_NONE;
    auto fb_shape = shape ? shape->serialize(builder).Union() : 0;
    auto fb_mesh = mesh ? mesh->serialize(builder) : 0;
    auto fb_material_type =
        material ? material->type() : snapshot::Material_NONE;
    auto fb_material = material ? material->serialize(builder).Union() : 0;

    std::vector<Offset<snapshot::Node>> child_vector;
    if (!children.empty()) {
      for (auto& child : children) {
        auto out = child->serialize(builder);
        child_vector.push_back(out);
      }
    }
    auto fb_children =
        child_vector.size() ? builder.CreateVector(child_vector) : 0;

    return snapshot::CreateNode(builder, fb_name, fb_transform, fb_shape_type,
                                fb_shape, fb_mesh, fb_material_type,
                                fb_material, fb_children);
  }
};

class SceneSerializer : public Serializer<snapshot::Scene> {
 public:
  snapshot::Vec3 camera;
  std::vector<std::shared_ptr<NodeSerializer>> nodes;

  virtual Offset<snapshot::Scene> serialize(
      FlatBufferBuilder& builder) override {
    std::vector<Offset<snapshot::Node>> nodes_vector;
    for (auto& node : nodes) {
      nodes_vector.push_back(node->serialize(builder));
    }
    return snapshot::CreateScene(builder, &camera,
                                 builder.CreateVector(nodes_vector));
  }
};

class ScenesSerializer : public Serializer<snapshot::Scenes> {
 public:
  std::vector<std::shared_ptr<SceneSerializer>> scenes;

  virtual Offset<snapshot::Scenes> serialize(
      FlatBufferBuilder& builder) override {
    std::vector<Offset<snapshot::Scene>> scenes_vector;
    for (auto& scene : scenes) {
      scenes_vector.push_back(scene->serialize(builder));
    }
    return snapshot::CreateScenes(builder, builder.CreateVector(scenes_vector));
  }
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_SNAPSHOT_SERIALIZER_H_
