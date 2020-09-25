// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_TESTS_FAKES_FAKE_COLOR_TRANSFORM_MANAGER_H_
#define SRC_UI_BIN_ROOT_PRESENTER_TESTS_FAKES_FAKE_COLOR_TRANSFORM_MANAGER_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

// Fakes out the ColorTransform Manager, which is the part of the Accessibility Manager responsible
// for notifying the system when the user requests a change in the current color transform.
class FakeColorTransformManager : public fuchsia::accessibility::ColorTransform {
 public:
  fidl::InterfaceRequestHandler<fuchsia::accessibility::ColorTransform> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return
        [this, dispatcher](fidl::InterfaceRequest<fuchsia::accessibility::ColorTransform> request) {
          bindings_.AddBinding(this, std::move(request), dispatcher);
        };
  }

  // Registers a color transform handler to receive updates about color correction and inversion
  // settings changes. Only one color transform handler at a time is supported.
  // |fuchsia::accessibility::ColorTransform|
  void RegisterColorTransformHandler(
      fidl::InterfaceHandle<fuchsia::accessibility::ColorTransformHandler> handle) {}

 private:
  fidl::BindingSet<fuchsia::accessibility::ColorTransform> bindings_;
};

#endif  // SRC_UI_BIN_ROOT_PRESENTER_TESTS_FAKES_FAKE_COLOR_TRANSFORM_MANAGER_H_
