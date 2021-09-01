// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/virtcon/session-manager.h"

#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "src/bringup/bin/virtcon/vc.h"

// These are necessary because vc-device.cc calls these functions and they need to be
// mocked out for testing. Ideally these files can be refactored to follow C++ patterns.
bool is_primary_bound() { return false; }
bool g_vc_owns_display = false;
void vc_gfx_invalidate_status(vc_gfx_t* gfx) {}
void vc_gfx_invalidate_all(vc_gfx_t* gfx, vc_t* vc) {}
void vc_gfx_invalidate(vc_gfx_t* gfx, vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {}
void vc_gfx_draw_char(vc_gfx_t* gfx, vc_t* vc, vc_char_t ch, unsigned x, unsigned y, bool invert) {}
void vc_attach_to_main_display(vc_t* vc) {}
void vc_toggle_framebuffer() {}

TEST(VirtconSessionManager, LifeTimeTest) {
  async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);
  virtcon::SessionManager sessions(loop.dispatcher(), false, &color_schemes[kDefaultColorScheme]);
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_pty::Device>();
  ASSERT_EQ(ZX_OK, endpoints.status_value());
  auto [local, remote] = *std::move(endpoints);
  ASSERT_EQ(ZX_OK, sessions.CreateSession(std::move(remote)).status_value());

  loop.Shutdown();
}

TEST(VirtconSessionManager, TestWriting) {
  async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);
  virtcon::SessionManager sessions(loop.dispatcher(), false, &color_schemes[kDefaultColorScheme]);
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_pty::Device>();
  ASSERT_EQ(ZX_OK, endpoints.status_value());
  auto [local, remote] = *std::move(endpoints);

  vc_t* session = nullptr;
  {
    auto response = sessions.CreateSession(std::move(remote));
    ASSERT_EQ(ZX_OK, response.status_value());
    session = std::move(response.value());
  }

  auto pty = fidl::WireClient(std::move(local), loop.dispatcher());

  char output[] = "Testing!";
  auto result = pty->Write_Sync(
      fidl::VectorView<uint8_t>::FromExternal(reinterpret_cast<uint8_t*>(output), sizeof(output)));
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_EQ(result->s, ZX_OK);
  ASSERT_EQ(result->actual, sizeof(output));

  loop.RunUntilIdle();

  // Check characters up to -1 since text_buf isn't null terminated.
  for (size_t i = 0; i < sizeof(output) - 1; i++) {
    ASSERT_EQ(vc_char_get_char(session->text_buf[i]), output[i], "index %ld : %c != %c", i,
              vc_char_get_char(session->text_buf[i]), output[i]);
  }
  loop.Shutdown();
}

TEST(VirtconSessionManager, TestWritingMultipleClients) {
  async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);
  virtcon::SessionManager sessions(loop.dispatcher(), false, &color_schemes[kDefaultColorScheme]);
  auto endpoints1 = fidl::CreateEndpoints<fuchsia_hardware_pty::Device>();
  ASSERT_EQ(ZX_OK, endpoints1.status_value());
  auto [local1, remote1] = *std::move(endpoints1);
  vc_t* session_one = nullptr;
  {
    auto response = sessions.CreateSession(std::move(remote1));
    ASSERT_EQ(ZX_OK, response.status_value());
    session_one = std::move(response.value());
  }
  auto pty_one = fidl::WireClient(std::move(local1), loop.dispatcher());

  auto endpoints2 = fidl::CreateEndpoints<fuchsia_hardware_pty::Device>();
  ASSERT_EQ(ZX_OK, endpoints2.status_value());
  auto [local2, remote2] = *std::move(endpoints2);
  vc_t* session_two = nullptr;
  {
    auto response = sessions.CreateSession(std::move(remote2));
    ASSERT_EQ(ZX_OK, response.status_value());
    session_two = std::move(response.value());
  }
  auto pty_two = fidl::WireClient(std::move(local2), loop.dispatcher());

  // Write pty_one.
  char output_one[] = "Testing One!";
  {
    auto result = pty_one->Write_Sync(fidl::VectorView<uint8_t>::FromExternal(
        reinterpret_cast<uint8_t*>(output_one), sizeof(output_one)));
    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_EQ(result->s, ZX_OK);
    ASSERT_EQ(result->actual, sizeof(output_one));
  }

  // Write pty_two.
  char output_two[] = "Testing One!";
  {
    auto result = pty_two->Write_Sync(fidl::VectorView<uint8_t>::FromExternal(
        reinterpret_cast<uint8_t*>(output_two), sizeof(output_two)));
    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_EQ(result->s, ZX_OK);
    ASSERT_EQ(result->actual, sizeof(output_two));
  }

  loop.RunUntilIdle();

  // Check output_one.
  // Check characters up to -1 since text_buf isn't null terminated.
  for (size_t i = 0; i < sizeof(output_one) - 1; i++) {
    ASSERT_EQ(vc_char_get_char(session_one->text_buf[i]), output_one[i], "index %ld : %c != %c", i,
              vc_char_get_char(session_one->text_buf[i]), output_one[i]);
  }

  // Check output_two.
  // Check characters up to -1 since text_buf isn't null terminated.
  for (size_t i = 0; i < sizeof(output_two) - 1; i++) {
    ASSERT_EQ(vc_char_get_char(session_two->text_buf[i]), output_two[i], "index %ld : %c != %c", i,
              vc_char_get_char(session_two->text_buf[i]), output_two[i]);
  }
  loop.Shutdown();
}

// Test that the log stays active with keep-log-visible.
TEST(VirtconSessionManager, KeepLogVisibleSessionStaysActive) {
  async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);

  zx::debuglog fake_log;
  ASSERT_EQ(log_start(loop.dispatcher(), std::move(fake_log), &color_schemes[kDefaultColorScheme]),
            0);

  virtcon::SessionManager sessions(loop.dispatcher(), true, &color_schemes[kDefaultColorScheme]);

  // Create the first session and check that it's not active.
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_pty::Device>();
  ASSERT_EQ(ZX_OK, endpoints.status_value());
  auto [local_one, remote] = *std::move(endpoints);

  vc_t* vc_one;
  {
    auto result = sessions.CreateSession(std::move(remote));
    ASSERT_TRUE(result.is_ok());
    vc_one = std::move(result.value());
  }
  ASSERT_TRUE(g_log_vc->active);
  ASSERT_FALSE(vc_one->active);

  log_delete_vc(g_log_vc);

  loop.Shutdown();
}

// Test that when we aren't keeping the log visible that the second session we create becomes
// the active one.
TEST(VirtconSessionManager, DontKeepLogVisibleSessionActivity) {
  async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);

  zx::debuglog fake_log;
  ASSERT_EQ(log_start(loop.dispatcher(), std::move(fake_log), &color_schemes[kDefaultColorScheme]),
            0);

  virtcon::SessionManager sessions(loop.dispatcher(), false, &color_schemes[kDefaultColorScheme]);

  // Create the first session and check that it's active.
  auto endpoints_one = fidl::CreateEndpoints<fuchsia_hardware_pty::Device>();
  ASSERT_EQ(ZX_OK, endpoints_one.status_value());
  auto [local_one, remote_one] = *std::move(endpoints_one);

  vc_t* vc_one;
  {
    auto result = sessions.CreateSession(std::move(remote_one));
    ASSERT_TRUE(result.is_ok());
    vc_one = std::move(result.value());
  }
  ASSERT_FALSE(g_log_vc->active);
  ASSERT_TRUE(vc_one->active);

  // Create the second session and check that it's not active.
  auto endpoints_two = fidl::CreateEndpoints<fuchsia_hardware_pty::Device>();
  ASSERT_EQ(ZX_OK, endpoints_two.status_value());
  auto [local_two, remote_two] = *std::move(endpoints_two);
  vc_t* vc_two;
  {
    auto result = sessions.CreateSession(std::move(remote_two));
    ASSERT_TRUE(result.is_ok());
    vc_two = std::move(result.value());
  }
  ASSERT_FALSE(g_log_vc->active);
  ASSERT_TRUE(vc_one->active);
  ASSERT_FALSE(vc_two->active);

  log_delete_vc(g_log_vc);

  loop.Shutdown();
}
