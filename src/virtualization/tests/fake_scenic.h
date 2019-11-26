// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_FAKE_SCENIC_H_
#define SRC_VIRTUALIZATION_TESTS_FAKE_SCENIC_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl_test_base.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <string.h>

#include <optional>
#include <unordered_map>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"

// This file implements a minimal fuschsia::ui::scenic::Scenic implementation.
//
// It is:
//
//  * Capable of broadcasting input events (such as keystrokes) to sessions.
//
//  * Implements a bare-minimum Scenic graphics API to allow a client to
//    create a simple View, create a framebuffer, and take screenshots of
//    that framebuffer.
//
// Caveat emptor: because this is strictly for tests, the fakes will
// FXL_CHECK-fail if clients send it unexpected data. We also require that, for
// screenshots to be taken, there be a single memory resource containing the
// relevant data.

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

// Raw screenshot data.
struct Screenshot {
  // Height and width of the image, in pixels.
  int height;
  int width;
  // Raw pixel data, 4 bytes per pixel, stored in RGBO format, one row at
  // a time.
  std::vector<std::byte> data;
};

class FakeSession : public fuchsia::ui::scenic::testing::Session_TestBase {
 public:
  FakeSession(fidl::InterfaceRequest<fuchsia::ui::scenic::Session> request,
              fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener);

  // Send an event or list of events to any attached listener.
  void SendEvent(fuchsia::ui::scenic::Event events);

  // Call the given callback if the connection has an error.
  void set_error_handler(fit::function<void(zx_status_t)> error_handler);

  // Take a screenshot of the current video output.
  //
  // |output| must be non-null, and will be initialized if the return code is
  // ZX_OK.
  zx_status_t CaptureScreenshot(Screenshot* output);

  // Height and Width of the virtual screen.
  static constexpr int kScreenWidthPixels = 1024;
  static constexpr int kScreenHeightPixels = 768;

 protected:
  // |fuchsia::ui::scenic::Session|
  void Present(uint64_t presentation_time, ::std::vector<::zx::event> acquire_fences,
               ::std::vector<::zx::event> release_fences, PresentCallback callback) override;

  // |fuchsia::ui::scenic::Session|
  void Enqueue(std::vector<fuchsia::ui::scenic::Command> cmds) override;

  // |fuchsia::ui::scenic::testing::Session_TestBase|
  void NotImplemented_(const std::string& name) final;

 private:
  // Find all resources of the given type.
  std::vector<const fuchsia::ui::gfx::CreateResourceCmd*> FindResourceByType(
      fuchsia::ui::gfx::ResourceArgs::Tag type);

  // Handler client events.
  void HandleGfxCommand(fuchsia::ui::gfx::Command cmd);
  void HandleGfxCreateResource(fuchsia::ui::gfx::CreateResourceCmd cmd);
  void HandleGfxReleaseResource(const fuchsia::ui::gfx::ReleaseResourceCmd& cmd);
  void HandleSetEventMask(const fuchsia::ui::gfx::SetEventMaskCmd& cmd);
  void HandleCreateView(uint32_t id);

  // Send a graphics event to the client.
  void SendGfxEvent(fuchsia::ui::gfx::Event event);

  // Resources created by our client.
  std::unordered_map<uint32_t, fuchsia::ui::gfx::CreateResourceCmd> resources_;

  // Client connection.
  fidl::InterfacePtr<fuchsia::ui::scenic::SessionListener> listener_;
  fidl::Binding<fuchsia::ui::scenic::Session> binding_;
};

class FakeScenic : public fuchsia::ui::scenic::testing::Scenic_TestBase {
 public:
  FakeScenic() = default;

  fidl::InterfaceRequestHandler<fuchsia::ui::scenic::Scenic> GetHandler();

  // Send an events to any attached listener.
  void SendEvent(fuchsia::ui::scenic::Event event);

  // Send a keyboard events to any attached listener.
  void SendKeyEvent(KeyboardEventHidUsage usage, fuchsia::ui::input::KeyboardEventPhase phase);

  // Send a keyboard "PRESS" and "RELEASE" event for the given key to any
  // attached listener.
  void SendKeyPress(KeyboardEventHidUsage usage);

  // Take a screenshot.
  //
  // |output| must be non-null, and will be initialized if the return code is
  // ZX_OK.
  zx_status_t CaptureScreenshot(Screenshot* output);

 protected:
  // |fuchsia::ui::scenic::Scenic|
  void CreateSession(fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
                     fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) override;

  // |fuchsia::ui::scenic::testing::Scenic_TestBase|
  void NotImplemented_(const std::string& name) final;

 private:
  // We only support a single session at a time. A session is active iff
  // session_ != nullopt.
  std::optional<FakeSession> session_;

  fidl::BindingSet<fuchsia::ui::scenic::Scenic> bindings_;
};

#endif  // SRC_VIRTUALIZATION_TESTS_FAKE_SCENIC_H_
