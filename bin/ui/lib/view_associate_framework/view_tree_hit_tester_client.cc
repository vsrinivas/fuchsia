// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_associate_framework/view_tree_hit_tester_client.h"

#include "lib/ftl/functional/closure.h"
#include "lib/ftl/logging.h"

namespace mozart {

ViewTreeHitTesterClient::ViewTreeHitTesterClient(
    const ftl::RefPtr<ViewInspectorClient>& view_inspector_client,
    ViewTreeTokenPtr view_tree_token)
    : view_inspector_client_(view_inspector_client),
      view_tree_token_(std::move(view_tree_token)),
      weak_factory_(this) {
  FTL_DCHECK(view_inspector_client_);
  FTL_DCHECK(view_tree_token_);

  UpdateHitTester();
}

ViewTreeHitTesterClient::~ViewTreeHitTesterClient() {}

void ViewTreeHitTesterClient::HitTest(PointFPtr point,
                                      const ResolvedHitsCallback& callback) {
  if (!hit_tester_) {
    callback(nullptr);
    return;
  }

  // TODO(jeffbrown): Here we are assuming that the hit test callbacks will be
  // invoked in FIFO order.  It might be a good idea to eliminate that
  // assumption.
  pending_callbacks_.push(callback);

  hit_tester_->HitTest(std::move(point), [this](HitTestResultPtr result) {
    OnHitTestResult(std::move(result));
  });
}

void ViewTreeHitTesterClient::OnHitTestResult(HitTestResultPtr result) {
  FTL_DCHECK(result);
  FTL_DCHECK(!pending_callbacks_.empty());

  view_inspector_client_->ResolveHits(std::move(result),
                                      pending_callbacks_.front());
  pending_callbacks_.pop();
}

void ViewTreeHitTesterClient::UpdateHitTester() {
  FTL_DCHECK(!hit_tester_);

  view_inspector_client_->view_inspector()->GetHitTester(
      view_tree_token_.Clone(), fidl::GetProxy(&hit_tester_),
      [ this, weak = weak_factory_.GetWeakPtr() ](bool renderer_changed) {
        if (weak)
          weak->OnHitTesterInvalidated(renderer_changed);
      });

  hit_tester_.set_connection_error_handler([this] { OnHitTesterDied(); });
}

void ViewTreeHitTesterClient::ReleaseHitTester() {
  hit_tester_.reset();

  while (!pending_callbacks_.empty()) {
    pending_callbacks_.front()(nullptr);
    pending_callbacks_.pop();
  }
}

void ViewTreeHitTesterClient::OnHitTesterInvalidated(bool renderer_changed) {
  ReleaseHitTester();

  if (renderer_changed)
    UpdateHitTester();

  if (hit_tester_changed_callback_)
    hit_tester_changed_callback_();
}

void ViewTreeHitTesterClient::OnHitTesterDied() {
  ReleaseHitTester();

  if (hit_tester_changed_callback_)
    hit_tester_changed_callback_();
}

}  // namespace mozart
