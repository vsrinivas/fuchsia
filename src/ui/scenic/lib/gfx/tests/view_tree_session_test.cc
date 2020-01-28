// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/tests/view_tree_session_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

void ViewTreeSessionTest::SetUp() {
  SessionTest::SetUp();
  if (session()) {
    RegisterSession(session());
  }
}

std::unique_ptr<Session> ViewTreeSessionTest::CreateAndRegisterSession() {
  auto session = CreateSession();
  RegisterSession(session.get());
  return session;
}

void ViewTreeSessionTest::RegisterSession(Session* session) {
  sessions_.push_back(session->GetWeakPtr());
}

void ViewTreeSessionTest::StageAndUpdateViewTree(SceneGraph* scene_graph) {
  for (const auto& session : sessions_) {
    if (session) {
      session->view_tree_updater()->StageViewTreeUpdates(scene_graph);
    }
  }
  scene_graph->ProcessViewTreeUpdates();

  // Clear unused sessions.
  std::vector<fxl::WeakPtr<Session>> new_sessions;
  std::copy_if(std::make_move_iterator(sessions_.begin()), std::make_move_iterator(sessions_.end()),
               std::back_inserter(new_sessions),
               [](const fxl::WeakPtr<Session>& p) -> bool { return bool(p); });
  sessions_ = std::move(new_sessions);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
