// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/compositor/src/graph/nodes.h"

#include <ostream>

#include "apps/compositor/glue/skia/type_converters.h"
#include "apps/compositor/services/cpp/formatting.h"
#include "apps/compositor/src/graph/scene_content.h"
#include "apps/compositor/src/graph/snapshot.h"
#include "apps/compositor/src/graph/transform_pair.h"
#include "apps/compositor/src/render/render_image.h"
#include "lib/ftl/logging.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkMatrix44.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkRect.h"

namespace compositor {
namespace {
SkColor MakeSkColor(const mojo::gfx::composition::Color& color) {
  return SkColorSetARGBInline(color.alpha, color.red, color.green, color.blue);
}

void SetPaintForBlend(SkPaint* paint, mojo::gfx::composition::Blend* blend) {
  FTL_DCHECK(paint);
  if (blend)
    paint->setAlpha(blend->alpha);
}

bool Contains(const SkRect& bounds, const SkPoint& point) {
  return point.x() >= bounds.left() && point.x() < bounds.right() &&
         point.y() >= bounds.top() && point.y() < bounds.bottom();
}
}  // namespace

Node::Node(uint32_t node_id,
           std::unique_ptr<TransformPair> content_transform,
           mojo::RectFPtr content_clip,
           mojo::gfx::composition::HitTestBehaviorPtr hit_test_behavior,
           Combinator combinator,
           const std::vector<uint32_t>& child_node_ids)
    : node_id_(node_id),
      content_transform_(std::move(content_transform)),
      content_clip_(content_clip.Pass()),
      hit_test_behavior_(hit_test_behavior.Pass()),
      combinator_(combinator),
      child_node_ids_(child_node_ids) {}

Node::~Node() {}

std::string Node::FormattedLabel(const SceneContent* content) const {
  return content->FormattedLabelForNode(node_id_);
}

bool Node::RecordContent(SceneContentBuilder* builder) const {
  FTL_DCHECK(builder);

  for (const auto& child_node_id : child_node_ids_) {
    if (!builder->RequireNode(child_node_id, node_id_))
      return false;
  }
  return true;
}

Snapshot::Disposition Node::RecordSnapshot(const SceneContent* content,
                                           SnapshotBuilder* builder) const {
  FTL_DCHECK(content);
  FTL_DCHECK(builder);

  switch (combinator_) {
    // MERGE: All or nothing.
    case Combinator::MERGE: {
      for (uint32_t child_node_id : child_node_ids_) {
        const Node* child_node = content->GetNode(child_node_id);
        FTL_DCHECK(child_node);
        Snapshot::Disposition disposition =
            builder->SnapshotNode(child_node, content);
        if (disposition == Snapshot::Disposition::kCycle)
          return disposition;
        if (disposition == Snapshot::Disposition::kBlocked) {
          if (builder->block_log()) {
            *builder->block_log()
                << "Node with MERGE combinator blocked since "
                   "one of its children is blocked: "
                << FormattedLabel(content) << ", blocked child "
                << child_node->FormattedLabel(content) << std::endl;
          }
          return disposition;
        }
      }
      return Snapshot::Disposition::kSuccess;
    }

    // PRUNE: Silently discard blocked children.
    case Combinator::PRUNE: {
      for (uint32_t child_node_id : child_node_ids_) {
        const Node* child_node = content->GetNode(child_node_id);
        FTL_DCHECK(child_node);
        Snapshot::Disposition disposition =
            builder->SnapshotNode(child_node, content);
        if (disposition == Snapshot::Disposition::kCycle)
          return disposition;
      }
      return Snapshot::Disposition::kSuccess;
    }

    // FALLBACK: Keep only the first unblocked child.
    case Combinator::FALLBACK: {
      if (child_node_ids_.empty())
        return Snapshot::Disposition::kSuccess;
      for (uint32_t child_node_id : child_node_ids_) {
        const Node* child_node = content->GetNode(child_node_id);
        FTL_DCHECK(child_node);
        Snapshot::Disposition disposition =
            builder->SnapshotNode(child_node, content);
        if (disposition != Snapshot::Disposition::kBlocked)
          return disposition;
      }
      if (builder->block_log()) {
        *builder->block_log() << "Node with FALLBACK combinator blocked since "
                                 "all of its children are blocked: "
                              << FormattedLabel(content) << std::endl;
      }
      return Snapshot::Disposition::kBlocked;
    }

    default: {
      if (builder->block_log()) {
        *builder->block_log()
            << "Unrecognized combinator: " << FormattedLabel(content)
            << std::endl;
      }
      return Snapshot::Disposition::kBlocked;
    }
  }
}

template <typename Func>
void Node::TraverseSnapshottedChildren(const SceneContent* content,
                                       const Snapshot* snapshot,
                                       const Func& func) const {
  FTL_DCHECK(content);
  FTL_DCHECK(snapshot);

  switch (combinator_) {
    // MERGE: All or nothing.
    case Combinator::MERGE: {
      for (uint32_t child_node_id : child_node_ids_) {
        const Node* child_node = content->GetNode(child_node_id);
        FTL_DCHECK(child_node);
        FTL_DCHECK(!snapshot->IsNodeBlocked(child_node));
        if (!func(child_node))
          return;
      }
      return;
    }

    // PRUNE: Silently discard blocked children.
    case Combinator::PRUNE: {
      for (uint32_t child_node_id : child_node_ids_) {
        const Node* child_node = content->GetNode(child_node_id);
        FTL_DCHECK(child_node);
        if (!snapshot->IsNodeBlocked(child_node) && !func(child_node))
          return;
      }
      return;
    }

    // FALLBACK: Keep only the first unblocked child.
    case Combinator::FALLBACK: {
      if (child_node_ids_.empty())
        return;
      for (uint32_t child_node_id : child_node_ids_) {
        const Node* child_node = content->GetNode(child_node_id);
        FTL_DCHECK(child_node);
        if (!snapshot->IsNodeBlocked(child_node)) {
          func(child_node);  // don't care about the result because we
          return;            // always stop after the first one
        }
      }
      FTL_NOTREACHED();
      return;
    }

    default: {
      FTL_NOTREACHED();
      return;
    }
  }
}

void Node::Paint(const SceneContent* content,
                 const Snapshot* snapshot,
                 SkCanvas* canvas) const {
  FTL_DCHECK(content);
  FTL_DCHECK(snapshot);
  FTL_DCHECK(canvas);

  const bool must_save = content_transform_ || content_clip_;
  if (must_save) {
    canvas->save();
    if (content_transform_)
      canvas->concat(content_transform_->forward());
    if (content_clip_)
      canvas->clipRect(content_clip_->To<SkRect>());
  }

  PaintInner(content, snapshot, canvas);

  if (must_save)
    canvas->restore();
}

void Node::PaintInner(const SceneContent* content,
                      const Snapshot* snapshot,
                      SkCanvas* canvas) const {
  FTL_DCHECK(content);
  FTL_DCHECK(snapshot);
  FTL_DCHECK(canvas);

  TraverseSnapshottedChildren(
      content, snapshot,
      [this, content, snapshot, canvas](const Node* child_node) -> bool {
        child_node->Paint(content, snapshot, canvas);
        return true;
      });
}

bool Node::HitTest(const SceneContent* content,
                   const Snapshot* snapshot,
                   const SkPoint& parent_point,
                   const SkMatrix44& global_to_parent_transform,
                   mojo::Array<mojo::gfx::composition::HitPtr>* hits) const {
  FTL_DCHECK(content);
  FTL_DCHECK(snapshot);
  FTL_DCHECK(hits);

  // TODO(jeffbrown): These calculations should probably be happening using
  // a 4x4 matrix instead.
  SkPoint local_point(parent_point);
  SkMatrix global_to_local_transform(global_to_parent_transform);
  if (content_transform_) {
    // TODO(jeffbrown): Defer matrix multiplications using a matrix stack.
    local_point = content_transform_->InverseMapPoint(parent_point);
    global_to_local_transform.preConcat(content_transform_->GetInverse());
  }

  if (content_clip_ && !Contains(content_clip_->To<SkRect>(), local_point))
    return false;

  bool opaque_children = false;
  if (!hit_test_behavior_ || !hit_test_behavior_->prune) {
    opaque_children = HitTestInner(content, snapshot, local_point,
                                   global_to_local_transform, hits);
  }

  return HitTestSelf(content, snapshot, local_point, global_to_local_transform,
                     hits) ||
         opaque_children;
}

bool Node::HitTestInner(
    const SceneContent* content,
    const Snapshot* snapshot,
    const SkPoint& local_point,
    const SkMatrix44& global_to_local_transform,
    mojo::Array<mojo::gfx::composition::HitPtr>* hits) const {
  FTL_DCHECK(content);
  FTL_DCHECK(snapshot);
  FTL_DCHECK(hits);

  // TODO(jeffbrown): Implement a more efficient way to traverse children in
  // reverse order.
  std::vector<const Node*> children;
  TraverseSnapshottedChildren(
      content, snapshot, [this, &children](const Node* child_node) -> bool {
        children.push_back(child_node);
        return true;
      });

  for (auto it = children.crbegin(); it != children.crend(); ++it) {
    if ((*it)->HitTest(content, snapshot, local_point,
                       global_to_local_transform, hits))
      return true;  // opaque child covering siblings
  }
  return false;
}

bool Node::HitTestSelf(
    const SceneContent* content,
    const Snapshot* snapshot,
    const SkPoint& local_point,
    const SkMatrix44& global_to_local_transform,
    mojo::Array<mojo::gfx::composition::HitPtr>* hits) const {
  FTL_DCHECK(content);
  FTL_DCHECK(snapshot);
  FTL_DCHECK(hits);

  if (!hit_test_behavior_ ||
      hit_test_behavior_->visibility ==
          mojo::gfx::composition::HitTestBehavior::Visibility::INVISIBLE)
    return false;

  if (hit_test_behavior_->hit_rect &&
      !Contains(hit_test_behavior_->hit_rect->To<SkRect>(), local_point))
    return false;

  auto hit = mojo::gfx::composition::Hit::New();
  hit->set_node(mojo::gfx::composition::NodeHit::New());
  hit->get_node()->node_id = node_id_;
  hit->get_node()->transform =
      mojo::ConvertTo<mojo::TransformPtr>(global_to_local_transform);
  hits->push_back(hit.Pass());
  return hit_test_behavior_->visibility ==
         mojo::gfx::composition::HitTestBehavior::Visibility::OPAQUE;
}

RectNode::RectNode(uint32_t node_id,
                   std::unique_ptr<TransformPair> content_transform,
                   mojo::RectFPtr content_clip,
                   mojo::gfx::composition::HitTestBehaviorPtr hit_test_behavior,
                   Combinator combinator,
                   const std::vector<uint32_t>& child_node_ids,
                   const mojo::RectF& content_rect,
                   const mojo::gfx::composition::Color& color)
    : Node(node_id,
           std::move(content_transform),
           content_clip.Pass(),
           hit_test_behavior.Pass(),
           combinator,
           child_node_ids),
      content_rect_(content_rect),
      color_(color) {}

RectNode::~RectNode() {}

void RectNode::PaintInner(const SceneContent* content,
                          const Snapshot* snapshot,
                          SkCanvas* canvas) const {
  FTL_DCHECK(content);
  FTL_DCHECK(snapshot);
  FTL_DCHECK(canvas);

  SkPaint paint;
  paint.setColor(MakeSkColor(color_));
  canvas->drawRect(content_rect_.To<SkRect>(), paint);

  Node::PaintInner(content, snapshot, canvas);
}

ImageNode::ImageNode(
    uint32_t node_id,
    std::unique_ptr<TransformPair> content_transform,
    mojo::RectFPtr content_clip,
    mojo::gfx::composition::HitTestBehaviorPtr hit_test_behavior,
    Combinator combinator,
    const std::vector<uint32_t>& child_node_ids,
    const mojo::RectF& content_rect,
    mojo::RectFPtr image_rect,
    uint32_t image_resource_id,
    mojo::gfx::composition::BlendPtr blend)
    : Node(node_id,
           std::move(content_transform),
           content_clip.Pass(),
           hit_test_behavior.Pass(),
           combinator,
           child_node_ids),
      content_rect_(content_rect),
      image_rect_(image_rect.Pass()),
      image_resource_id_(image_resource_id),
      blend_(blend.Pass()) {}

ImageNode::~ImageNode() {}

bool ImageNode::RecordContent(SceneContentBuilder* builder) const {
  FTL_DCHECK(builder);

  return Node::RecordContent(builder) &&
         builder->RequireResource(image_resource_id_, Resource::Type::kImage,
                                  node_id());
}

void ImageNode::PaintInner(const SceneContent* content,
                           const Snapshot* snapshot,
                           SkCanvas* canvas) const {
  FTL_DCHECK(content);
  FTL_DCHECK(snapshot);
  FTL_DCHECK(canvas);

  auto image_resource = static_cast<const ImageResource*>(
      content->GetResource(image_resource_id_, Resource::Type::kImage));
  FTL_DCHECK(image_resource);

  SkPaint paint;
  SetPaintForBlend(&paint, blend_.get());

  canvas->drawImageRect(image_resource->image()->image().get(),
                        image_rect_
                            ? image_rect_->To<SkRect>()
                            : SkRect::MakeWH(image_resource->image()->width(),
                                             image_resource->image()->height()),
                        content_rect_.To<SkRect>(), &paint);

  Node::PaintInner(content, snapshot, canvas);
}

SceneNode::SceneNode(
    uint32_t node_id,
    std::unique_ptr<TransformPair> content_transform,
    mojo::RectFPtr content_clip,
    mojo::gfx::composition::HitTestBehaviorPtr hit_test_behavior,
    Combinator combinator,
    const std::vector<uint32_t>& child_node_ids,
    uint32_t scene_resource_id,
    uint32_t scene_version)
    : Node(node_id,
           std::move(content_transform),
           content_clip.Pass(),
           hit_test_behavior.Pass(),
           combinator,
           child_node_ids),
      scene_resource_id_(scene_resource_id),
      scene_version_(scene_version) {}

SceneNode::~SceneNode() {}

bool SceneNode::RecordContent(SceneContentBuilder* builder) const {
  FTL_DCHECK(builder);

  return Node::RecordContent(builder) &&
         builder->RequireResource(scene_resource_id_, Resource::Type::kScene,
                                  node_id());
}

Snapshot::Disposition SceneNode::RecordSnapshot(
    const SceneContent* content,
    SnapshotBuilder* builder) const {
  FTL_DCHECK(content);
  FTL_DCHECK(builder);

  Snapshot::Disposition disposition =
      builder->SnapshotReferencedScene(this, content);
  if (disposition != Snapshot::Disposition::kSuccess)
    return disposition;
  return Node::RecordSnapshot(content, builder);
}

void SceneNode::PaintInner(const SceneContent* content,
                           const Snapshot* snapshot,
                           SkCanvas* canvas) const {
  FTL_DCHECK(content);
  FTL_DCHECK(snapshot);
  FTL_DCHECK(canvas);

  const SceneContent* resolved_content =
      snapshot->GetResolvedSceneContent(this);
  FTL_DCHECK(resolved_content);
  resolved_content->Paint(snapshot, canvas);

  Node::PaintInner(content, snapshot, canvas);
}

bool SceneNode::HitTestInner(
    const SceneContent* content,
    const Snapshot* snapshot,
    const SkPoint& local_point,
    const SkMatrix44& global_to_local_transform,
    mojo::Array<mojo::gfx::composition::HitPtr>* hits) const {
  FTL_DCHECK(content);
  FTL_DCHECK(snapshot);
  FTL_DCHECK(hits);

  if (Node::HitTestInner(content, snapshot, local_point,
                         global_to_local_transform, hits))
    return true;  // opaque child covering referenced scene

  const SceneContent* resolved_content =
      snapshot->GetResolvedSceneContent(this);
  FTL_DCHECK(resolved_content);

  mojo::gfx::composition::SceneHitPtr scene_hit;
  bool opaque = resolved_content->HitTest(
      snapshot, local_point, global_to_local_transform, &scene_hit);
  if (scene_hit) {
    auto hit = mojo::gfx::composition::Hit::New();
    hit->set_scene(scene_hit.Pass());
    hits->push_back(hit.Pass());
  }
  return opaque;
}

LayerNode::LayerNode(
    uint32_t node_id,
    std::unique_ptr<TransformPair> content_transform,
    mojo::RectFPtr content_clip,
    mojo::gfx::composition::HitTestBehaviorPtr hit_test_behavior,
    Combinator combinator,
    const std::vector<uint32_t>& child_node_ids,
    const mojo::RectF& layer_rect,
    mojo::gfx::composition::BlendPtr blend)
    : Node(node_id,
           std::move(content_transform),
           content_clip.Pass(),
           hit_test_behavior.Pass(),
           combinator,
           child_node_ids),
      layer_rect_(layer_rect),
      blend_(blend.Pass()) {}

LayerNode::~LayerNode() {}

void LayerNode::PaintInner(const SceneContent* content,
                           const Snapshot* snapshot,
                           SkCanvas* canvas) const {
  FTL_DCHECK(content);
  FTL_DCHECK(snapshot);
  FTL_DCHECK(canvas);

  SkPaint paint;
  SetPaintForBlend(&paint, blend_.get());

  canvas->saveLayer(layer_rect_.To<SkRect>(), &paint);
  Node::PaintInner(content, snapshot, canvas);
  canvas->restore();
}

}  // namespace compositor
