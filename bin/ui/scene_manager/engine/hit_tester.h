// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/bin/ui/scene_manager/engine/hit.h"

namespace scene_manager {

class Node;
class Session;

// Performs a hit test on the contents of a node.
class HitTester {
 public:
  HitTester();
  ~HitTester();

  // Performs a hit test along the specified ray.
  // Returns a list of hits sorted by increasing distance then by increasing
  // tree depth.  See the |Session.HitTest()| API for more information.
  std::vector<Hit> HitTest(Node* node, const escher::ray4& ray);

 private:
  // Describes a possible hit within an enclosing tag node.
  struct TagInfo {
    static constexpr float kNoHit = std::numeric_limits<float>::infinity();

    // The distance to the intersection as defined by |Hit.distance|.
    float distance = kNoHit;

    bool is_hit() const { return distance < kNoHit; }

    void ReportIntersection(float d) {
      if (d < distance)
        distance = d;
    }
  };

  // Describes a ray and its accumulated transform.
  struct RayInfo {
    // The ray to test in the object's coordinate system.
    escher::ray4 ray;

    // The accumulated inverse transformation matrix which maps the coordinate
    // system of the node at which the hit test was initiated into the
    // coordinate system of the object.
    escher::mat4 inverse_transform;
  };

  // Accumulates hit test results from the node, as seen by its parent.
  // Applies the node's transform to the ray stack.
  // |ray_info_| must be in the parent's local coordinate system.
  void AccumulateHitsOuter(Node* node);

  // Accumulates hit test results from the node, as seen by the node itself.
  // Applies the node's tag to the tag stack.
  // |ray_info_| must be in the node's local coordinate system.
  void AccumulateHitsLocal(Node* node);

  // Accumulates hit test results from the node's content and children.
  // |ray_info_| must be in the node's local coordinate system.
  void AccumulateHitsInner(Node* node);

  // Returns true if the ray passes through the node's parts.
  // |ray| must be in the node's local coordinate system.
  static bool IsRayWithinPartsInner(Node* node, const escher::ray4& ray);

  // Returns true if the ray passes through the node's clipped content.
  // |ray| must be in the parent's local coordinate system.
  //
  // TODO(MZ-207): The way this works only makes geometric sense if the ray
  // is parallel to the camera projection at the point being sampled.
  static bool IsRayWithinClippedContentOuter(Node* node,
                                             const escher::ray4& ray);

  // Returns true if the ray passes through the node's clipped content.
  // |ray| must be in the node's local coordinate system.
  static bool IsRayWithinClippedContentInner(Node* node,
                                             const escher::ray4& ray);

  // The vector which accumulates hits.
  std::vector<Hit> hits_;

  // The session in which the hit test was initiated.
  // Only nodes belonging to this session will be considered.
  Session* session_ = nullptr;

  // The current tag information.
  // Null if there is no enclosing tagged node.
  TagInfo* tag_info_ = nullptr;

  // The current ray information.
  // Null if there is no hit test currently in progress.
  RayInfo* ray_info_ = nullptr;
};

}  // namespace scene_manager
