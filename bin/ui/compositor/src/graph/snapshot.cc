// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/compositor/src/graph/snapshot.h"

#include "apps/compositor/glue/skia/type_converters.h"
#include "apps/compositor/services/cpp/formatting.h"
#include "apps/compositor/src/graph/scene_content.h"
#include "lib/ftl/logging.h"
#include "third_party/skia/include/core/SkMatrix44.h"
#include "third_party/skia/include/core/SkPictureRecorder.h"
#include "third_party/skia/include/core/SkRect.h"

namespace compositor {

Snapshot::Snapshot() {}

Snapshot::~Snapshot() {}

bool Snapshot::HasDependency(
    const mojo::gfx::composition::SceneToken& scene_token) const {
  return dependencies_.find(scene_token.value) != dependencies_.end();
}

ftl::RefPtr<RenderFrame> Snapshot::Paint(const RenderFrame::Metadata& metadata,
                                         const mojo::Rect& viewport) const {
  FTL_DCHECK(!is_blocked());
  FTL_DCHECK(root_scene_content_);

  SkIRect sk_viewport = viewport.To<SkIRect>();

  SkPictureRecorder recorder;
  recorder.beginRecording(SkRect::Make(sk_viewport));
  root_scene_content_->Paint(this, recorder.getRecordingCanvas());
  return ftl::MakeRefCounted<RenderFrame>(metadata, sk_viewport,
                                          recorder.finishRecordingAsPicture());
}

void Snapshot::HitTest(const mojo::PointF& point,
                       mojo::gfx::composition::HitTestResult* result) const {
  FTL_DCHECK(result);
  FTL_DCHECK(!is_blocked());
  FTL_DCHECK(root_scene_content_);

  root_scene_content_->HitTest(this, point.To<SkPoint>(), SkMatrix44::I(),
                               &result->root);
}

bool Snapshot::IsNodeBlocked(const Node* node) const {
  FTL_DCHECK(!is_blocked());

  auto it = node_dispositions_.find(node);
  FTL_DCHECK(it != node_dispositions_.end());
  FTL_DCHECK(it->second == Disposition::kSuccess ||
             it->second == Disposition::kBlocked);
  return it->second == Disposition::kBlocked;
}

const SceneContent* Snapshot::GetResolvedSceneContent(
    const SceneNode* scene_node) const {
  FTL_DCHECK(!is_blocked());

  auto it = resolved_scene_contents_.find(scene_node);
  FTL_DCHECK(it != resolved_scene_contents_.end());
  return it->second.get();
}

SnapshotBuilder::SnapshotBuilder(std::ostream* block_log)
    : snapshot_(new Snapshot()), block_log_(block_log) {}

SnapshotBuilder::~SnapshotBuilder() {}

Snapshot::Disposition SnapshotBuilder::SnapshotNode(
    const Node* node,
    const SceneContent* content) {
  FTL_DCHECK(snapshot_);
  FTL_DCHECK(node);
  FTL_DCHECK(content);

  auto it = snapshot_->node_dispositions_.find(node);
  if (it != snapshot_->node_dispositions_.end())
    return it->second;

  Snapshot::Disposition disposition = node->RecordSnapshot(content, this);
  snapshot_->node_dispositions_[node] = disposition;
  return disposition;
}

Snapshot::Disposition SnapshotBuilder::SnapshotReferencedScene(
    const SceneNode* referrer_node,
    const SceneContent* referrer_content) {
  FTL_DCHECK(snapshot_);
  FTL_DCHECK(referrer_node);
  FTL_DCHECK(referrer_content);

  // This function should only ever be called once when snapshotting the
  // referring |SceneNode| at which point the result will be memoized
  // by |SnapshotNode| as usual so reentrance should not occur.
  FTL_DCHECK(snapshot_->resolved_scene_contents_.find(referrer_node) ==
             snapshot_->resolved_scene_contents_.end());

  auto scene_resource =
      static_cast<const SceneResource*>(referrer_content->GetResource(
          referrer_node->scene_resource_id(), Resource::Type::kScene));
  FTL_DCHECK(scene_resource);

  ftl::RefPtr<const SceneContent> content;
  Snapshot::Disposition disposition = AddDependencyResolveAndSnapshotScene(
      scene_resource->scene_token(), referrer_node->scene_version(), &content);

  if (disposition == Snapshot::Disposition::kSuccess) {
    snapshot_->resolved_scene_contents_[referrer_node] = content;
  } else if (disposition == Snapshot::Disposition::kBlocked) {
    if (block_log_) {
      *block_log_ << "Scene node's referenced scene is blocked: "
                  << referrer_node->FormattedLabel(referrer_content)
                  << ", referenced scene " << scene_resource->scene_token()
                  << ", version " << referrer_node->scene_version()
                  << std::endl;
    }
  }
  return disposition;
}

Snapshot::Disposition SnapshotBuilder::SnapshotSceneContent(
    const SceneContent* content) {
  FTL_DCHECK(snapshot_);
  FTL_DCHECK(content);

  const Node* root = content->GetRootNodeIfExists();
  if (!root) {
    if (block_log_) {
      *block_log_ << "Scene has no root node: " << content->FormattedLabel()
                  << std::endl;
    }
    return Snapshot::Disposition::kBlocked;
  }

  return SnapshotNode(root, content);
}

Snapshot::Disposition SnapshotBuilder::AddDependencyResolveAndSnapshotScene(
    const mojo::gfx::composition::SceneToken& scene_token,
    uint32_t version,
    ftl::RefPtr<const SceneContent>* out_content) {
  FTL_DCHECK(out_content);

  snapshot_->dependencies_.insert(scene_token.value);
  return ResolveAndSnapshotScene(scene_token, version, out_content);
}

ftl::RefPtr<const Snapshot> SnapshotBuilder::Build(
    const mojo::gfx::composition::SceneToken& scene_token,
    uint32_t version) {
  FTL_DCHECK(snapshot_);
  FTL_DCHECK(!snapshot_->root_scene_content_);

  ftl::RefPtr<const SceneContent> content;
  snapshot_->disposition_ =
      AddDependencyResolveAndSnapshotScene(scene_token, version, &content);

  if (!snapshot_->is_blocked()) {
    snapshot_->root_scene_content_ = content;
  } else {
    snapshot_->resolved_scene_contents_.clear();
    snapshot_->node_dispositions_.clear();
  }
  return std::move(snapshot_);
}

}  // namespace compositor
