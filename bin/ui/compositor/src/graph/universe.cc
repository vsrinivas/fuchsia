// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/compositor/src/graph/universe.h"

#include "apps/compositor/services/cpp/formatting.h"
#include "apps/compositor/src/graph/scene_content.h"
#include "lib/ftl/logging.h"

namespace compositor {

Universe::Universe() {}

Universe::~Universe() {}

void Universe::AddScene(const SceneLabel& label) {
  FTL_DCHECK(scenes_.find(label.token()) == scenes_.end());
  scenes_.emplace(label.token(),
                  std::unique_ptr<SceneInfo>(new SceneInfo(label)));
}

void Universe::PresentScene(const ftl::RefPtr<const SceneContent>& content) {
  auto it = scenes_.find(content->label().token());
  FTL_DCHECK(it != scenes_.end());
  it->second->content_queue.emplace_front(content);
}

void Universe::RemoveScene(
    const mojo::gfx::composition::SceneToken& scene_token) {
  auto it = scenes_.find(scene_token.value);
  FTL_DCHECK(it != scenes_.end());
  scenes_.erase(it);
}

ftl::RefPtr<const Snapshot> Universe::SnapshotScene(
    const mojo::gfx::composition::SceneToken& scene_token,
    uint32_t version,
    std::ostream* block_log) {
  generation_++;
  FTL_CHECK(generation_);

  Snapshotter snapshotter(this, block_log);
  ftl::RefPtr<const Snapshot> snapshot =
      snapshotter.Build(scene_token, version);

  // TODO(jeffbrown): Find a better way to prune unused scene versions.
  // This logic is expensive and will break if there are multiple renderers
  // involved.  Perhaps we should do something like say that all renderers
  // that live in the same universe should get snapshotted simultaneously.
  // Or maybe we could be smarter and partition scenes by reachability
  // using some heuristics to decide which partition should be the scene's
  // "primary" in case of conflict (so that scenes which only appear on a
  // single display can be rate-coupled to that display whereas those that
  // appear in multiple places might experience some resampling).
  // There's a similar problem lurking in the scheduler mechanism.
  for (const auto& pair : scenes_) {
    SceneInfo* info = pair.second.get();
    if (info->update_generation != generation_ &&
        info->content_queue.size() > 1) {
      info->content_queue.erase(info->content_queue.begin() + 1,
                                info->content_queue.end());
    }
  }
  return snapshot;
}

Universe::SceneInfo::SceneInfo(const SceneLabel& label) : label(label) {}

Universe::SceneInfo::~SceneInfo() {}

Universe::Snapshotter::Snapshotter(Universe* universe, std::ostream* block_log)
    : SnapshotBuilder(block_log), universe_(universe) {
  FTL_DCHECK(universe_);
}

Universe::Snapshotter::~Snapshotter() {
  FTL_DCHECK(!cycle_);  // must have properly unwound any cycles by now
}

Snapshot::Disposition Universe::Snapshotter::ResolveAndSnapshotScene(
    const mojo::gfx::composition::SceneToken& scene_token,
    uint32_t version,
    ftl::RefPtr<const SceneContent>* out_content) {
  auto it = universe_->scenes_.find(scene_token.value);
  if (it == universe_->scenes_.end()) {
    if (block_log()) {
      *block_log() << "Scene not available: " << scene_token << std::endl;
    }
    return Snapshot::Disposition::kBlocked;
  }

  // TODO(jeffbrown): Ok, this logic is downright terrible.  It will end
  // up doing N^2 work when things are blocked.  Replace this with some kind
  // of sane invalidation mechanism before the system explodes.
  SceneInfo* info = it->second.get();
  if (info->update_generation == universe_->generation_) {
    if (info->disposition == Snapshot::Disposition::kCycle) {
      cycle_ = info;  // start unwinding, remember where to stop
      return Snapshot::Disposition::kCycle;
    }
    if (info->disposition == Snapshot::Disposition::kBlocked) {
      if (block_log()) {
        *block_log() << "Scene blocked (cached prior disposition): "
                     << info->label.FormattedLabel() << std::endl;
      }
      return Snapshot::Disposition::kBlocked;
    }
  } else {
    info->update_generation = universe_->generation_;
    if (info->content_queue.empty()) {
      if (block_log()) {
        *block_log() << "Scene has not presented any content: "
                     << info->label.FormattedLabel() << std::endl;
      }
      info->disposition = Snapshot::Disposition::kBlocked;
      return Snapshot::Disposition::kBlocked;
    }

    auto it = info->content_queue.begin();
    for (;;) {
      info->disposition = Snapshot::Disposition::kCycle;
      info->disposition = SnapshotSceneContent((*it).get());
      if (info->disposition == Snapshot::Disposition::kSuccess)
        break;
      if (info->disposition == Snapshot::Disposition::kCycle) {
        FTL_DCHECK(cycle_);
        if (block_log()) {
          *block_log() << "Scene is part of a cycle: "
                       << (*it)->FormattedLabel() << std::endl;
        }
        if (cycle_ == info)
          cycle_ = nullptr;  // found the ouroboros tail, stop unwinding
        info->disposition = Snapshot::Disposition::kBlocked;
        return Snapshot::Disposition::kCycle;
      }
      if (++it == info->content_queue.end())
        return Snapshot::Disposition::kBlocked;
    }
    if (it + 1 != info->content_queue.end())
      info->content_queue.erase(it + 1, info->content_queue.end());
  }

  FTL_DCHECK(info->disposition == Snapshot::Disposition::kSuccess);
  FTL_DCHECK(!info->content_queue.empty());
  const ftl::RefPtr<const SceneContent>& content = info->content_queue.back();
  if (!content->MatchesVersion(version)) {
    if (block_log()) {
      *block_log() << "Scene version mismatch: " << info->label.FormattedLabel()
                   << ", requested version " << version
                   << ", available version " << content->version() << std::endl;
    }
    return Snapshot::Disposition::kBlocked;
  }

  *out_content = content;
  return Snapshot::Disposition::kSuccess;
}

}  // namespace compositor
