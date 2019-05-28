// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_FAKE_SCENIC_H_
#define SRC_VIRTUALIZATION_TESTS_FAKE_SCENIC_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl_test_base.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fit/defer.h>
#include <lib/svc/cpp/services.h>
#include <src/lib/fxl/logging.h>
#include <string.h>

// This file implements a minimal fuschsia::ui::scenic::Scenic implementation
// capable of broadcasting input events (such as keystrokes) to sessions.
//
// No graphics facilities are provided.

// Key codes used in keyboard events.
//
// Scenic uses HID key codes, such as the table at:
// <https://source.android.com/devices/input/keyboard-devices>
//
// We only represent a small number of possible keys; those required for tests.
enum class KeyboardEventHidUsage : uint16_t {
  KEY_A = 0x04,
  KEY_B = 0x05,
  KEY_C = 0x06,
  KEY_RETURN = 0x28,
  KEY_ESC = 0x29,
  KEY_LSHIFT = 0xe1,
};

class FakeInputOnlySession
    : public fuchsia::ui::scenic::testing::Session_TestBase {
 public:
  FakeInputOnlySession() = default;

  // Bind a session request to this session.
  void Bind(
      fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
      fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener);

  // Broadcast an event to all listeners.
  void BroadcastEvent(const fuchsia::ui::scenic::Event& event);

 protected:
  void NotImplemented_(const std::string& name) final;

 private:
  fidl::BindingSet<fuchsia::ui::scenic::Session> bindings_;
  fidl::InterfacePtrSet<fuchsia::ui::scenic::SessionListener> listeners_;
};

class FakeInputOnlyScenic
    : public fuchsia::ui::scenic::testing::Scenic_TestBase {
 public:
  FakeInputOnlyScenic() = default;

  fidl::InterfaceRequestHandler<fuchsia::ui::scenic::Scenic> GetHandler();

  // Broadcast an event to all listeners.
  void BroadcastEvent(const fuchsia::ui::scenic::Event& event);

  // Broadcast a keyboard event to all listeners.
  void BroadcastKeyEvent(KeyboardEventHidUsage usage,
                         fuchsia::ui::input::KeyboardEventPhase phase);

 protected:
  // |fuchsia::ui::scenic::Scenic|
  void CreateSession(
      fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
      fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener)
      override;

  void NotImplemented_(const std::string& name) final;

 private:
  fidl::BindingSet<fuchsia::ui::scenic::Scenic> bindings_;
  fidl::InterfaceHandle<fuchsia::ui::scenic::Scenic> real_scenic_;
  FakeInputOnlySession session_;
};

#endif  // SRC_VIRTUALIZATION_TESTS_FAKE_SCENIC_H_
