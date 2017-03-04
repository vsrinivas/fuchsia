// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_INPUT_MANAGER_HIT_RESOLVER_H_
#define APPS_MOZART_SRC_INPUT_MANAGER_HIT_RESOLVER_H_

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "apps/mozart/lib/view_associate_framework/view_inspector_client.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "apps/mozart/services/views/view_trees.fidl.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/mtl/tasks/message_loop.h"

namespace input_manager {

class InputAssociate;

struct EventPath {
  mozart::ViewTokenPtr token;
  mozart::TransformPtr transform;
  std::unique_ptr<EventPath> next;

  std::unique_ptr<EventPath> Clone() {
    std::unique_ptr<EventPath> clone(new EventPath());
    clone->token = token.Clone();
    clone->transform = transform.Clone();
    if (next) {
      clone->next = next->Clone();
    }
    return clone;
  }
};

using OnResolvedCallback =
    std::function<void(std::vector<std::unique_ptr<EventPath>> views)>;

class ViewHitResolver {
 public:
  explicit ViewHitResolver(InputAssociate* associate);
  ~ViewHitResolver();

  struct ViewHitNode {
    std::unique_ptr<EventPath> event_path_;

    ViewHitNode* parent_;
    std::vector<std::unique_ptr<ViewHitNode>> children_;

    bool was_hit_ = false;
    bool skipped_ = true;
    std::vector<mozart::ViewTokenPtr> hit_children_;

    friend bool operator== ( const ViewHitNode &lhs, const ViewHitNode &rhs);
  };

  void Resolve(const mozart::SceneHit* root_scene,
               mozart::PointFPtr point,
               std::unique_ptr<mozart::ResolvedHits> resolved_hits,
               OnResolvedCallback callback);

 private:
  struct Resolution {
    std::unique_ptr<ViewHitNode> root_;
    // ViewHitNode owned through |root_| and its |children_|
    std::vector<ViewHitNode*> candidates_;
    uint64_t candidates_count_ = 0;
    // ViewToken is held by an EventPath which is held by a
    // ViewHitNode which is eventually held by through the
    // descendent |children_| of |root_|
    std::map<mozart::ViewToken*, ViewHitNode*> nodes_;

    bool cancelled_ = false;
    ViewHitResolver* resolver_;
    OnResolvedCallback callback_;
    ftl::WeakPtrFactory<Resolution> weak_ptr_factory_;
    ftl::RefPtr<ftl::TaskRunner> task_runner_;

    Resolution(ViewHitResolver* resolver, OnResolvedCallback callback);
    ~Resolution();

    void OnViewHitTestResult(ViewHitNode* node,
                             bool was_hit,
                             fidl::Array<mozart::ViewTokenPtr> views);
    void OnCompleted();
    void Watch();
  };

  Resolution* CreateResolution(OnResolvedCallback callback);

  InputAssociate* const associate_;
  std::vector<std::unique_ptr<Resolution>> resolutions_;
};

std::ostream& operator<<(std::ostream& os,
                         const ViewHitResolver::ViewHitNode& value);
std::ostream& operator<<(std::ostream& os,
                         const ViewHitResolver::ViewHitNode* value);

}  // namespace input_manager

#endif
