// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/ui/associates/view_tree_hit_tester_client.h"

#include "base/bind.h"
#include "base/logging.h"

namespace mojo {
namespace ui {

ViewTreeHitTesterClient::ViewTreeHitTesterClient(
    const scoped_refptr<ViewInspectorClient>& view_inspector_client,
    mojo::ui::ViewTreeTokenPtr view_tree_token)
    : view_inspector_client_(view_inspector_client),
      view_tree_token_(view_tree_token.Pass()),
      weak_factory_(this) {
  DCHECK(view_inspector_client_);
  DCHECK(view_tree_token_);

  UpdateHitTester();
}

ViewTreeHitTesterClient::~ViewTreeHitTesterClient() {}

void ViewTreeHitTesterClient::HitTest(mojo::PointFPtr point,
                                      const ResolvedHitsCallback& callback) {
  if (!hit_tester_) {
    callback.Run(nullptr);
    return;
  }

  // TODO(jeffbrown): Here we are assuming that the hit test callbacks will be
  // invoked in FIFO order.  It might be a good idea to eliminate that
  // assumption.
  pending_callbacks_.push(callback);
  hit_tester_->HitTest(point.Pass(),
                       base::Bind(&ViewTreeHitTesterClient::OnHitTestResult,
                                  base::Unretained(this)));
}

void ViewTreeHitTesterClient::OnHitTestResult(
    mojo::gfx::composition::HitTestResultPtr result) {
  DCHECK(result);
  DCHECK(!pending_callbacks_.empty());

  view_inspector_client_->ResolveHits(result.Pass(),
                                      pending_callbacks_.front());
  pending_callbacks_.pop();
}

void ViewTreeHitTesterClient::UpdateHitTester() {
  DCHECK(!hit_tester_);

  view_inspector_client_->view_inspector()->GetHitTester(
      view_tree_token_.Clone(), mojo::GetProxy(&hit_tester_),
      base::Bind(&ViewTreeHitTesterClient::OnHitTesterInvalidated,
                 weak_factory_.GetWeakPtr()));

  hit_tester_.set_connection_error_handler(base::Bind(
      &ViewTreeHitTesterClient::OnHitTesterDied, base::Unretained(this)));
}

void ViewTreeHitTesterClient::ReleaseHitTester() {
  hit_tester_.reset();

  while (!pending_callbacks_.empty()) {
    pending_callbacks_.front().Run(nullptr);
    pending_callbacks_.pop();
  }
}

void ViewTreeHitTesterClient::OnHitTesterInvalidated(bool renderer_changed) {
  ReleaseHitTester();

  if (renderer_changed)
    UpdateHitTester();

  if (!hit_tester_changed_callback_.is_null())
    hit_tester_changed_callback_.Run();
}

void ViewTreeHitTesterClient::OnHitTesterDied() {
  ReleaseHitTester();

  if (!hit_tester_changed_callback_.is_null())
    hit_tester_changed_callback_.Run();
}

}  // namespace ui
}  // namespace mojo
