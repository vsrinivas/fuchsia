// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_GFX_COMPOSITOR_GRAPH_UNIVERSE_H_
#define SERVICES_GFX_COMPOSITOR_GRAPH_UNIVERSE_H_

#include <deque>
#include <iosfwd>

#include "apps/compositor/src/graph/scene_label.h"
#include "apps/compositor/src/graph/snapshot.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"

namespace compositor {

class SceneContent;
class Snapshot;

// Manages all active or pending versions of all scenes in the entire universe.
//
// Currently there is only one instance of the universe (this could change
// someday).  Its job is to efficiently build snapshots for rendering
// subject to the following invariants.
//
// 1. Scene state evolution always progresses forwards in time.  At no time
//    will an older version of a scene be included in a snapshot once a
//    newer version becomes unblocked.  This is true even when the scene is
//    being rendered in multiple places.
//
// 2. A scene dependency which does not specify an explicit version (by
//    passing |kSceneVersionNone|) will never be blocked as long as the
//    dependent scene still exists and has published at least one unblocked
//    version.  (Clients should watch for |OnResourceUnavailable| to handle
//    the case where a dependent scene spontaneously becomes unavailable.)
//
// 3. A scene dependency which specifies an explicit version may become
//    blocked or unblocked as the dependent scene publishes newer unblocked
//    scene versions.
//
// 4. Scene dependency cycles are resolved by considering all scenes within
//    the cycle to be blocked.  This guarantees consistent behavior regardless
//    of how the cycle is entered.
//
// TODO(jeffbrown): In principle this object could keep track of scene
// invalidations and incremental updates.
class Universe {
 public:
  Universe();
  ~Universe();

  void AddScene(const SceneLabel& scene_label);
  void PresentScene(const ftl::RefPtr<const SceneContent>& content);
  void RemoveScene(const mojo::gfx::composition::SceneToken& scene_token);

  ftl::RefPtr<const Snapshot> SnapshotScene(
      const mojo::gfx::composition::SceneToken& scene_token,
      uint32_t version,
      std::ostream* block_log);

 private:
  struct SceneInfo {
    SceneInfo(const SceneLabel& label);
    ~SceneInfo();

    SceneLabel label;

    // Set to the current generation when the queue was last updated.
    // TODO(jeffbrown): We should perform more fine-grained invalidation of
    // scenes based on their dependencies.
    uint64_t update_generation = 0u;
    Snapshot::Disposition disposition = Snapshot::Disposition::kBlocked;
    std::deque<ftl::RefPtr<const SceneContent>> content_queue;
  };

  class Snapshotter : public SnapshotBuilder {
   public:
    Snapshotter(Universe* universe, std::ostream* block_log);
    ~Snapshotter() override;

   protected:
    Snapshot::Disposition ResolveAndSnapshotScene(
        const mojo::gfx::composition::SceneToken& scene_token,
        uint32_t version,
        ftl::RefPtr<const SceneContent>* out_content) override;

   private:
    Universe* universe_;
    SceneInfo* cycle_ = nullptr;

    FTL_DISALLOW_COPY_AND_ASSIGN(Snapshotter);
  };

  std::unordered_map<uint32_t, std::unique_ptr<SceneInfo>> scenes_;
  uint64_t generation_ = 0u;

  FTL_DISALLOW_COPY_AND_ASSIGN(Universe);
};

}  // namespace compositor

#endif  // SERVICES_GFX_COMPOSITOR_GRAPH_UNIVERSE_H_
