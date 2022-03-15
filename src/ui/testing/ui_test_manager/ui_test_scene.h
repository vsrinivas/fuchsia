// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UI_TEST_MANAGER_UI_TEST_SCENE_H_
#define SRC_UI_TESTING_UI_TEST_MANAGER_UI_TEST_SCENE_H_

// Creates the root of the scene (either by calling to scene manager/root presenter)
// or by direct construction.
class UITestScene {
 public:
  virtual ~UITestScene() = default;

  // Initializes the scene root.
  virtual void Initialize() = 0;

  // Attaches a client view to the scene via fuchsia.ui.app.ViewProvider.
  virtual void AttachClientView() = 0;

  // Returns true when the client view has been attached to the scene.
  // In order to be consider "attached to the scene", there must be a connected
  // path from the scene root to the client view, and the client view must have
  // presented at least one frame of content.
  virtual bool ClientViewIsAttached() = 0;
};

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_UI_TEST_SCENE_H_
