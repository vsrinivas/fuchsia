// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_GRAPH_SCENE_DEF_H_
#define APPS_MOZART_SRC_COMPOSITOR_GRAPH_SCENE_DEF_H_

#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "apps/mozart/services/composition/interfaces/scenes.mojom.h"
#include "apps/mozart/src/compositor/graph/nodes.h"
#include "apps/mozart/src/compositor/graph/resources.h"
#include "apps/mozart/src/compositor/graph/scene_content.h"
#include "apps/mozart/src/compositor/graph/scene_label.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"

namespace compositor {

class SceneDef;
class Universe;

// Determines whether a scene is registered.
using SceneResolver = std::function<bool(const mozart::SceneToken&)>;

// Sends a scene unavailable message with the specified resource id.
using SceneUnavailableSender = std::function<void(uint32_t)>;

// Scene definition.
//
// Contains the client-supplied content that makes up a scene in an
// incrementally updatable form.  As part of preparing the scene for
// presentation, the content is gathered up into an immutable
// |SceneContent| object.
class SceneDef {
 public:
  // Outcome of a call to |Present|.
  enum class Disposition {
    kUnchanged,
    kSucceeded,
    kFailed,
  };

  SceneDef(const SceneLabel& label);
  ~SceneDef();

  // Gets the scene label.
  const SceneLabel& label() const { return label_; }
  std::string FormattedLabel() const { return label_.FormattedLabel(); }

  // Enqueues a pending update event to the scene graph.
  void EnqueueUpdate(mozart::SceneUpdatePtr update);

  // Enqueues a pending publish event to the scene graph.
  // The changes are not applied until |ApplyChanges| is called.
  void EnqueuePublish(mozart::SceneMetadataPtr metadata);

  // Applies published updates to the scene up to the point indicated by
  // |presentation_time|, adds new scene content to the universe.
  //
  // Returns a value which indicates whether the updates succeeded.
  // If the result is |kFailed|, the scene graph was left in an unusable
  // and inconsistent state and must be destroyed.
  Disposition Present(ftl::TimePoint presentation_time,
                      Universe* universe,
                      const SceneResolver& resolver,
                      const SceneUnavailableSender& unavailable_sender,
                      std::ostream& err);

  // Reports that a scene has been unregistered.
  // Causes |OnResourceUnavailable()| to be delivered for all matching scene
  // references.
  void NotifySceneUnavailable(const mozart::SceneToken& scene_token,
                              const SceneUnavailableSender& unavailable_sender);

 private:
  class Collector : public SceneContentBuilder {
   public:
    Collector(const SceneDef* scene,
              uint32_t version,
              ftl::TimePoint presentation_time,
              std::ostream& err);
    ~Collector() override;

   protected:
    const Resource* FindResource(uint32_t resource_id) const override;
    const Node* FindNode(uint32_t node_id) const override;

   private:
    const SceneDef* scene_;

    FTL_DISALLOW_COPY_AND_ASSIGN(Collector);
  };

  struct Publication {
    Publication(mozart::SceneMetadataPtr metadata);
    ~Publication();

    bool is_due(ftl::TimePoint presentation_time) const {
      return ftl::TimePoint::FromEpochDelta(ftl::TimeDelta::FromNanoseconds(
                 metadata->presentation_time)) <= presentation_time;
    }

    mozart::SceneMetadataPtr metadata;
    std::vector<mozart::SceneUpdatePtr> updates;

   private:
    FTL_DISALLOW_COPY_AND_ASSIGN(Publication);
  };

  bool ApplyUpdate(mozart::SceneUpdatePtr update,
                   const SceneResolver& resolver,
                   const SceneUnavailableSender& unavailable_sender,
                   std::ostream& err);

  ftl::RefPtr<const Resource> CreateResource(
      uint32_t resource_id,
      mozart::ResourcePtr resource_decl,
      const SceneResolver& resolver,
      const SceneUnavailableSender& unavailable_sender,
      std::ostream& err);
  ftl::RefPtr<const Node> CreateNode(uint32_t node_id,
                                     mozart::NodePtr node_decl,
                                     std::ostream& err);

  const SceneLabel label_;

  std::vector<mozart::SceneUpdatePtr> pending_updates_;
  std::vector<std::unique_ptr<Publication>> pending_publications_;

  std::unordered_map<uint32_t, ftl::RefPtr<const Resource>> resources_;
  std::unordered_map<uint32_t, ftl::RefPtr<const Node>> nodes_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SceneDef);
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_GRAPH_SCENE_DEF_H_
