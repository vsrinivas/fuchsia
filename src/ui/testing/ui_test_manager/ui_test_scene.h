// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UI_TEST_MANAGER_UI_TEST_SCENE_H_
#define SRC_UI_TESTING_UI_TEST_MANAGER_UI_TEST_SCENE_H_

#include <lib/sys/component/cpp/testing/realm_builder.h>

#include <optional>

namespace ui_testing {

// Creates the root of the scene (either by calling to scene manager/root presenter)
// or by direct construction.
class UITestScene {
 public:
  virtual ~UITestScene() = default;

  // Initializes the scene root, and attaches a client view.
  virtual void Initialize() = 0;

  // Returns true when the client view is attached to the scene.
  // In order to be consider "attached to the scene", there must be a connected
  // path from the scene root to the client view.
  virtual bool ClientViewIsAttached() = 0;

  // Returns true if the client view is attached to the scene, AND the client
  // has presented at least one frame of content.
  virtual bool ClientViewIsRendering() = 0;

  // Returns the view ref koid for the client view if it's been attached to the
  // scene, and std::nullopt otherwise.
  virtual std::optional<zx_koid_t> ClientViewRefKoid() = 0;

  // Returns the scale factor for the client view (if any).
  virtual float ClientViewScaleFactor() = 0;
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_UI_TEST_SCENE_H_
