// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_TESTS_MOCKS_MOCK_GESTURE_LISTENER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_TESTS_MOCKS_MOCK_GESTURE_LISTENER_H_

#include <fuchsia/accessibility/gesture/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/string.h>

#include <string>

namespace accessibility_test {

class MockGestureListener : public fuchsia::accessibility::gesture::Listener {
 public:
  MockGestureListener();

  ~MockGestureListener() = default;

  // Returns new binding for the Listener.
  fidl::InterfaceHandle<Listener> NewBinding();

  // Returns |is_registered_|.
  bool is_registered() const;

  // Sets |utterance_| which will be sent in the callback when OnGesture() is called.
  void SetUtterance(std::string utterance);

  // Sets |on_gesture_callback_status| which will be sent in the callback when OnGesture() is
  // called.
  void SetOnGestureCallbackStatus(bool status);

  // Sets |gesture_type_| which will be overridden when OnGesture() is called.
  void SetGestureType(fuchsia::accessibility::gesture::Type gesture_type);

  fuchsia::accessibility::gesture::Type gesture_type() const;

 private:
  // |fuchsia::accessibility::gesture::Listener|
  void OnGesture(fuchsia::accessibility::gesture::Type gesture_type,
                 Listener::OnGestureCallback callback) override;

  // Stores utterance which will be sent in the callback when OnGesture() is called.
  fidl::StringPtr utterance_ = cpp17::nullopt;

  // Status sent in the callback when OnGesture() is called.
  bool on_gesture_callback_status_ = true;

  fidl::Binding<Listener> binding_;

  // Stores |gesture_type| on which OnGesture was called.
  fuchsia::accessibility::gesture::Type gesture_type_;

  // Tracks if the listener is binded.
  bool is_registered_ = false;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_TESTS_MOCKS_MOCK_GESTURE_LISTENER_H_
