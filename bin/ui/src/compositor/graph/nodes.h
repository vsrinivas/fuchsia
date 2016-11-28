// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_GRAPH_NODES_H_
#define APPS_MOZART_SRC_COMPOSITOR_GRAPH_NODES_H_

#include <iosfwd>
#include <memory>
#include <vector>

#include "apps/mozart/services/composition/nodes.fidl.h"
#include "apps/mozart/src/compositor/graph/paint_context.h"
#include "apps/mozart/src/compositor/graph/snapshot.h"
#include "apps/tracing/lib/trace/dump.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"

class SkCanvas;
struct SkPoint;
class SkMatrix44;

namespace compositor {

class SceneContent;
class SceneContentBuilder;
class TransformPair;

// Base class for nodes in a scene graph.
//
// The base class mainly acts as a container for other nodes and does not
// draw any content of its own.
//
// Instances of this class are immutable and reference counted so they may
// be shared by multiple versions of the same scene.
class Node : public ftl::RefCountedThreadSafe<Node> {
 public:
  using Combinator = mozart::Node::Combinator;

  Node(uint32_t node_id,
       std::unique_ptr<TransformPair> content_transform,
       mozart::RectFPtr content_clip,
       mozart::HitTestBehaviorPtr hit_test_behavior,
       Combinator combinator,
       const std::vector<uint32_t>& child_node_ids);

  uint32_t node_id() const { return node_id_; }
  const TransformPair* content_transform() const {
    return content_transform_.get();
  }
  const mozart::HitTestBehavior* hit_test_behavior() const {
    return hit_test_behavior_.get();
  }
  const mozart::RectF* content_clip() const { return content_clip_.get(); }
  Combinator combinator() const { return combinator_; }
  const std::vector<uint32_t>& child_node_ids() const {
    return child_node_ids_;
  }

  // Gets a descriptive label.
  std::string FormattedLabel(const SceneContent* content) const;

  // Dumps a description of the resource.
  virtual void Dump(tracing::Dump* dump) const;

  // Called by the scene content builder to traverse the node's dependencies
  // recursively and ensure they are included in the scene's local content.
  // Returns true if successful, false if the node contains linkage errors.
  virtual bool RecordContent(SceneContentBuilder* builder) const;

  // Called by the snapshot builder to traverse the node's dependencies
  // recursively follow links into other scenes, evaluate whether the
  // node can be rendered, and record which path was taken for the purposes
  // of satisfying combinators.
  virtual Snapshot::Disposition RecordSnapshot(const SceneContent* content,
                                               SnapshotBuilder* builder) const;

  // Paints the content of the node to a recording canvas.
  void Paint(const SceneContent* content,
             const Snapshot* snapshot,
             PaintContext* context) const;

  // Performs a hit test at the specified point.
  // The |point| is the hit tested point in the parent's coordinate space.
  // The |global_to_parent_transform| is the accumulated transform from the
  // global coordinate space to the parent's coordinate space.
  // Adds hit information for the node to |hits|.
  // Returns true if the search was terminated by an opaque hit.
  bool HitTest(const SceneContent* content,
               const Snapshot* snapshot,
               const SkPoint& parent_point,
               const SkMatrix44& global_to_parent_transform,
               fidl::Array<mozart::HitPtr>* hits) const;

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(Node);
  virtual ~Node();

  void DumpCommon(tracing::Dump* dump) const;

  // Applies a unary function to the children selected by the node's
  // combinator rule during a snapshot.
  // Stops when |Func| returns false.
  // |Func| should have the signature |bool func(const Node*)|.
  template <typename Func>
  void TraverseSnapshottedChildren(const SceneContent* content,
                                   const Snapshot* snapshot,
                                   const Func& func) const;

  virtual void PaintInner(const SceneContent* content,
                          const Snapshot* snapshot,
                          PaintContext* context) const;

  virtual bool HitTestInner(const SceneContent* content,
                            const Snapshot* snapshot,
                            const SkPoint& local_point,
                            const SkMatrix44& global_to_local_transform,
                            fidl::Array<mozart::HitPtr>* hits) const;

 private:
  bool HitTestSelf(const SceneContent* content,
                   const Snapshot* snapshot,
                   const SkPoint& local_point,
                   const SkMatrix44& global_to_local_transform,
                   fidl::Array<mozart::HitPtr>* hits) const;

  uint32_t const node_id_;
  std::unique_ptr<TransformPair> const content_transform_;
  mozart::RectFPtr const content_clip_;
  mozart::HitTestBehaviorPtr const hit_test_behavior_;
  Combinator const combinator_;
  std::vector<uint32_t> const child_node_ids_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Node);
};

// Represents a rectangle node.
//
// Draws a solid color filled rectangle node underneath its children.
class RectNode : public Node {
 public:
  RectNode(uint32_t node_id,
           std::unique_ptr<TransformPair> content_transform,
           mozart::RectFPtr content_clip,
           mozart::HitTestBehaviorPtr hit_test_behavior,
           Combinator combinator,
           const std::vector<uint32_t>& child_node_ids,
           const mozart::RectF& content_rect,
           const mozart::Color& color);

  const mozart::RectF& content_rect() const { return content_rect_; }
  const mozart::Color& color() const { return color_; }

  void Dump(tracing::Dump* dump) const override;

 protected:
  ~RectNode() override;

  void PaintInner(const SceneContent* content,
                  const Snapshot* snapshot,
                  PaintContext* context) const override;

 private:
  mozart::RectF const content_rect_;
  mozart::Color const color_;

  FTL_DISALLOW_COPY_AND_ASSIGN(RectNode);
};

// Represents an image node.
//
// Draws an image filled rectangle underneath its children.
class ImageNode : public Node {
 public:
  ImageNode(uint32_t node_id,
            std::unique_ptr<TransformPair> content_transform,
            mozart::RectFPtr content_clip,
            mozart::HitTestBehaviorPtr hit_test_behavior,
            Combinator combinator,
            const std::vector<uint32_t>& child_node_ids,
            const mozart::RectF& content_rect,
            mozart::RectFPtr image_rect,
            uint32_t image_resource_id,
            mozart::BlendPtr blend);

  const mozart::RectF& content_rect() const { return content_rect_; }
  const mozart::RectF* image_rect() const { return image_rect_.get(); }
  uint32_t image_resource_id() const { return image_resource_id_; }
  const mozart::Blend* blend() const { return blend_.get(); }

  void Dump(tracing::Dump* dump) const override;
  bool RecordContent(SceneContentBuilder* builder) const override;

 protected:
  ~ImageNode() override;

  void PaintInner(const SceneContent* content,
                  const Snapshot* snapshot,
                  PaintContext* context) const override;

 private:
  mozart::RectF const content_rect_;
  mozart::RectFPtr const image_rect_;
  uint32_t const image_resource_id_;
  mozart::BlendPtr const blend_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ImageNode);
};

// Represents a scene node.
//
// Draws an embedded scene underneath its children.
class SceneNode : public Node {
 public:
  SceneNode(uint32_t node_id,
            std::unique_ptr<TransformPair> content_transform,
            mozart::RectFPtr content_clip,
            mozart::HitTestBehaviorPtr hit_test_behavior,
            Combinator combinator,
            const std::vector<uint32_t>& child_node_ids,
            uint32_t scene_resource_id,
            uint32_t scene_version);

  uint32_t scene_resource_id() const { return scene_resource_id_; }
  uint32_t scene_version() const { return scene_version_; }

  void Dump(tracing::Dump* dump) const override;
  bool RecordContent(SceneContentBuilder* builder) const override;
  Snapshot::Disposition RecordSnapshot(const SceneContent* content,
                                       SnapshotBuilder* builder) const override;

 protected:
  ~SceneNode() override;

  void PaintInner(const SceneContent* content,
                  const Snapshot* snapshot,
                  PaintContext* context) const override;

  bool HitTestInner(const SceneContent* content,
                    const Snapshot* snapshot,
                    const SkPoint& local_point,
                    const SkMatrix44& global_to_local_transform,
                    fidl::Array<mozart::HitPtr>* hits) const override;

 private:
  uint32_t const scene_resource_id_;
  uint32_t const scene_version_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SceneNode);
};

// Represents a layer node.
//
// Composites its children to a layer and applies a blending operation.
class LayerNode : public Node {
 public:
  LayerNode(uint32_t node_id,
            std::unique_ptr<TransformPair> content_transform,
            mozart::RectFPtr content_clip,
            mozart::HitTestBehaviorPtr hit_test_behavior,
            Combinator combinator,
            const std::vector<uint32_t>& child_node_ids,
            const mozart::RectF& layer_rect,
            mozart::BlendPtr blend);

  const mozart::RectF& layer_rect() const { return layer_rect_; }
  const mozart::Blend* blend() const { return blend_.get(); }

  void Dump(tracing::Dump* dump) const override;

 protected:
  ~LayerNode() override;

  void PaintInner(const SceneContent* content,
                  const Snapshot* snapshot,
                  PaintContext* context) const override;

 private:
  mozart::RectF const layer_rect_;
  mozart::BlendPtr const blend_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LayerNode);
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_GRAPH_NODES_H_
