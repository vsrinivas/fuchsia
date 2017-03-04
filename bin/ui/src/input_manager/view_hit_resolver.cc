// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_manager/view_hit_resolver.h"

#include <queue>

#include "apps/mozart/services/geometry/cpp/geometry_util.h"
#include "apps/mozart/services/views/cpp/formatting.h"
#include "apps/mozart/src/input_manager/input_associate.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_delta.h"

namespace input_manager {

constexpr ftl::TimeDelta kHitTestReplyTimeout =
    ftl::TimeDelta::FromMilliseconds(30);

std::ostream& operator<<(std::ostream& os,
                         const ViewHitResolver::ViewHitNode& value) {
  os << "{view=" << *value.event_path_->token;
  if (value.parent_) {
    os << ", parent=" << *(value.parent_->event_path_->token);
  }
  os << ", children=[";
  bool first = true;
  for (auto const& node : value.children_) {
    if (first)
      first = false;
    else
      os << ", ";
    os << node.get();
  }
  return os << "]}";
}

std::ostream& operator<<(std::ostream& os,
                         const ViewHitResolver::ViewHitNode* value) {
  return value ? os << *value : os << "null";
}

bool operator==(const ViewHitResolver::ViewHitNode& lhs,
                const ViewHitResolver::ViewHitNode& rhs) {
  return lhs.event_path_->token->value == rhs.event_path_->token->value;
}

ViewHitResolver::ViewHitResolver(InputAssociate* associate)
    : associate_(associate) {}

ViewHitResolver::~ViewHitResolver() {}

ViewHitResolver::Resolution* ViewHitResolver::CreateResolution(
    OnResolvedCallback callback) {
  std::unique_ptr<Resolution> resolution(new Resolution(this, callback));
  Resolution* ptr = resolution.get();
  // Cancel all previous ones
  for (auto const& r : resolutions_) {
    r->cancelled_ = true;
  }
  // Keep hold of the resolution
  resolutions_.push_back(std::move(resolution));
  return ptr;
}

void ViewHitResolver::Resolve(
    const mozart::SceneHit* root_scene,
    mozart::PointFPtr point,
    std::unique_ptr<mozart::ResolvedHits> resolved_hits,
    OnResolvedCallback callback) {
  std::queue<std::pair<const mozart::SceneHit*, ViewHitNode*>> nodes;
  nodes.push(std::make_pair(root_scene, nullptr));

  Resolution* resolution = CreateResolution(callback);

  // Construct tree from SceneHit
  while (!nodes.empty()) {
    auto entry = nodes.front();
    const mozart::SceneHit* scene = entry.first;
    ViewHitNode* parent = entry.second;
    nodes.pop();

    ViewHitNode* current = parent;
    auto it = resolved_hits->map().find(scene->scene_token->value);
    if (it != resolved_hits->map().end()) {
      // Scene is associated to a view, need to traverse
      std::unique_ptr<ViewHitNode> node(new ViewHitNode());
      current = node.get();
      node->event_path_ = std::make_unique<EventPath>();
      node->event_path_->token = it->second.Clone();
      node->event_path_->transform = scene->transform.Clone();

      resolution->candidates_.push_back(node.get());
      resolution->nodes_[node->event_path_->token.get()] = node.get();

      if (parent) {
        // We're already traversing, build event path as we go
        node->parent_ = parent;
        node->event_path_->next = parent->event_path_->Clone();
        // Keep a pointer in the parent to the new child created
        parent->children_.push_back(std::move(node));
      } else {
        // This is the root
        resolution->root_ = std::move(node);
      }
    }

    // Add sub-scenes to the tree travseral
    for (size_t i = 0; i < scene->hits.size(); ++i) {
      if (scene->hits[i]->is_scene()) {
        nodes.push(std::make_pair(scene->hits[i]->get_scene().get(), current));
      }
    }
  }

  FTL_VLOG(1) << "ViewHitTest Tree: " << resolution->root_.get();

  // Simulatenously ViewHitTest views
  resolution->Watch();
  resolution->candidates_count_ = resolution->candidates_.size();
  for (auto node : resolution->candidates_) {
    auto transformed = TransformPoint(*(node->event_path_->transform), *point);
    auto p = mozart::PointF::New();
    p->x = transformed.x;
    p->y = transformed.y;

    FTL_VLOG(1) << "ViewHitTesting: " << node;
    associate_->ViewHitTest(
        node->event_path_->token.get(), std::move(p),
        [node, resolution](bool was_hit,
                           fidl::Array<mozart::ViewTokenPtr> views) mutable {
          resolution->OnViewHitTestResult(node, was_hit, std::move(views));
        });
  }
}

#pragma mark - ViewHitResolver::Resolution

ViewHitResolver::Resolution::Resolution(ViewHitResolver* resolver,
                                        OnResolvedCallback callback)
    : resolver_(resolver),
      callback_(callback),
      weak_ptr_factory_(this),
      task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()) {}

ViewHitResolver::Resolution::~Resolution() {}

void ViewHitResolver::Resolution::OnViewHitTestResult(
    ViewHitNode* node,
    bool was_hit,
    fidl::Array<mozart::ViewTokenPtr> views) {
  FTL_VLOG(1) << "OnViewHitTestResult: node = " << node
              << ", was_hit = " << was_hit << ", subviews = " << views;

  auto it = std::find(candidates_.begin(), candidates_.end(), node);
  if (candidates_.end() != it) {
    candidates_count_--;
    node->was_hit_ = was_hit;
    if (views) {
      node->skipped_ = false;
      for (auto& child : views) {
        node->hit_children_.push_back(std::move(child));
      }
    }
  }

  if (candidates_count_ == 0) {
    FTL_VLOG(1) << "ViewHitTest got all results";
    OnCompleted();
  }
}

void ViewHitResolver::Resolution::OnCompleted() {
  if (!cancelled_) {
    cancelled_ = true;

    FTL_VLOG(1) << "ViewHitTest completed";
    std::queue<ViewHitNode*> nodes;
    nodes.push(root_.get());
    std::vector<ViewHitNode*> views_hit;
    // Walk the tree to find leaves that are ViewHitConnection
    while (!nodes.empty()) {
      auto node = nodes.front();
      nodes.pop();
      if (node->was_hit_) {
        if (node->hit_children_.size()) {
          for (auto& child : node->hit_children_) {
            nodes.push(nodes_[child.get()]);
          }
        } else if (node->skipped_ && node->children_.size()) {
          for (auto& child : node->children_) {
            nodes.push(child.get());
          }
        } else {
          FTL_VLOG(1) << "Resolved candidate: " << node;
          views_hit.push_back(node);
        }
      }
    }

    // Forward list of views that should get input event to caller
    std::vector<std::unique_ptr<EventPath>> result;
    for (auto view_hit : views_hit) {
      result.push_back(std::move(view_hit->event_path_));
    }
    callback_(std::move(result));
  }

  // Remove reference to self
  for (auto it = resolver_->resolutions_.begin();
       it != resolver_->resolutions_.end(); ++it) {
    if ((*it).get() == this) {
      resolver_->resolutions_.erase(it);
      break;
    }
  }
}

void ViewHitResolver::Resolution::Watch() {
  task_runner_->PostDelayedTask(
      [weak = weak_ptr_factory_.GetWeakPtr()] {
        if (weak) {
          FTL_VLOG(1) << "ViewHitTest timed out";
          weak->OnCompleted();
        }
      },
      kHitTestReplyTimeout);
}

}  // namespace input_manager
