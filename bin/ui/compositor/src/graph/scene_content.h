// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_GFX_COMPOSITOR_GRAPH_SCENE_CONTENT_H_
#define SERVICES_GFX_COMPOSITOR_GRAPH_SCENE_CONTENT_H_

#include <iosfwd>
#include <string>
#include <unordered_map>

#include "apps/compositor/services/interfaces/hit_tests.mojom.h"
#include "apps/compositor/services/interfaces/scenes.mojom.h"
#include "apps/compositor/src/graph/nodes.h"
#include "apps/compositor/src/graph/resources.h"
#include "apps/compositor/src/graph/scene_label.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"

class SkCanvas;
struct SkPoint;
class SkMatrix44;

namespace compositor {

class SceneContentBuilder;

// Represents the content of a particular published version of a scene.
//
// Holds a resource and node table which describes the content of a
// scene as it was when a particular version was published.  Only the
// internal state of the scene is described; links to other scenes are
// not resolved at this level.
//
// Once fully constructed, instances of this class are immutable and
// reference counted so they may be bound to scene references in other scenes.
//
// TODO(jeffbrown): To improve efficiency, we could replace the hash tables
// with a vector of internally linked graph edges.  This is relatively easy
// since the traversal order is well-known and we could even build some kind
// of hierarchical iterator to walk the graph starting from the root.
class SceneContent : public ftl::RefCountedThreadSafe<SceneContent> {
 public:
  // Gets the scene label.
  const SceneLabel& label() const { return label_; }
  std::string FormattedLabel() const {
    return label_.FormattedLabelForVersion(version_, presentation_time_);
  }
  std::string FormattedLabelForNode(uint32_t node_id) const {
    return label_.FormattedLabelForNode(version_, presentation_time_, node_id);
  }

  // Gets the version of the scene represented by this object.
  uint32_t version() const { return version_; }

  // Gets the time when this scene was presented.
  int64_t presentation_time() const { return presentation_time_; }

  // Returns true if this content satisfies a request for the specified version.
  bool MatchesVersion(uint32_t requested_version) const;

  // Paints the content of the scene to a recording canvas.
  void Paint(const Snapshot* snapshot, SkCanvas* canvas) const;

  // Performs a hit test at the specified point.
  // The |scene_point| is the hit tested point in the scene's coordinate space.
  // The |global_to_scene_transform| is the accumulated transform from the
  // global coordinate space to the scene's coordinate space.
  // Provides hit information for the scene in |out_scene_hit| if any.
  // Returns true if the search was terminated by an opaque hit.
  bool HitTest(const Snapshot* snapshot,
               const SkPoint& scene_point,
               const SkMatrix44& global_to_scene_transform,
               mojo::gfx::composition::SceneHitPtr* out_scene_hit) const;

  // Gets the requested resource, never null because it must be present.
  const Resource* GetResource(uint32_t resource_id,
                              Resource::Type resource_type) const;

  // Gets the requested node, never null because it must be present.
  const Node* GetNode(uint32_t node_id) const;

  // Gets the root node if it exists, otherwise returns nullptr.
  const Node* GetRootNodeIfExists() const;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(SceneContent);
  friend class SceneContentBuilder;
  SceneContent(const SceneLabel& label,
               uint32_t version,
               int64_t presentation_time,
               size_t max_resources,
               size_t max_nodes);
  ~SceneContent();

  const SceneLabel label_;
  const uint32_t version_;
  const int64_t presentation_time_;
  std::unordered_map<uint32_t, ftl::RefPtr<const Resource>> resources_;
  std::unordered_map<uint32_t, ftl::RefPtr<const Node>> nodes_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SceneContent);
};

// Builds a table of all of the nodes and resources that make up the
// content of a particular version of a scene.
class SceneContentBuilder {
 public:
  SceneContentBuilder(const SceneLabel& label,
                      uint32_t version,
                      int64_t presentation_time,
                      size_t max_resources,
                      size_t max_nodes,
                      std::ostream& err);
  virtual ~SceneContentBuilder();

  // Stream for reporting validation error messages.
  std::ostream& err() { return err_; }

  // Ensures the requested resource is part of the retained scene graph and
  // returns a reference to it, or nullptr if an error occurred.
  const Resource* RequireResource(uint32_t resource_id,
                                  Resource::Type resource_type,
                                  uint32_t referrer_node_id);

  // Ensures the requested node is part of the retained scene graph and
  // returns a reference to it, or nullptr if an error occurred.
  const Node* RequireNode(uint32_t node_id, uint32_t referrer_node_id);

  // Builds the content graph.
  // Returns nullptr if an error occurred.
  ftl::RefPtr<const SceneContent> Build();

 protected:
  // Finds resources or nodes in the current version, returns nullptr if absent.
  virtual const Resource* FindResource(uint32_t resource_id) const = 0;
  virtual const Node* FindNode(uint32_t node_id) const = 0;

 private:
  bool AddNode(const Node* node);

  ftl::RefPtr<SceneContent> content_;
  std::ostream& err_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SceneContentBuilder);
};

}  // namespace compositor

#endif  // SERVICES_GFX_COMPOSITOR_GRAPH_SCENE_CONTENT_H_
