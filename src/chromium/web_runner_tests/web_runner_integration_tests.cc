// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/chromium/web_runner_tests/test_server.h"
#include "src/chromium/web_runner_tests/web_context.h"
#include "src/lib/fxl/strings/string_printf.h"

// This file contains a subset of adapted Chromium Fuchsia tests to make sure
// nothing broke on the import boundary.
//
// See also: https://chromium.googlesource.com/chromium/src/+/master/fuchsia
namespace {

// This is a black box smoke test for whether the web runner in a given system
// is capable of performing basic operations.
//
// This tests if launching a component with an HTTP URL triggers an HTTP GET for
// the main resource, and if an HTML response with an <img> tag triggers a
// subresource load for the image.
//
// See also:
// https://chromium.googlesource.com/chromium/src/+/master/fuchsia/runners/web/web_runner_smoke_test.cc
TEST(WebRunnerIntegrationTest, Smoke) {
  web_runner_tests::TestServer server;
  FX_CHECK(server.FindAndBindPort());

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = fxl::StringPrintf("http://localhost:%d/foo.html", server.port());

  fuchsia::sys::LauncherSyncPtr launcher;
  sys::ServiceDirectory::CreateFromNamespace()->Connect(launcher.NewRequest());

  fuchsia::sys::ComponentControllerSyncPtr controller;
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  ASSERT_TRUE(server.Accept());

  std::string expected_prefix = "GET /foo.html HTTP";
  // We need to overallocate the first time to drain the read since we expect
  // the subsresource load on the same connection.
  std::string buf(4096, 0);
  ASSERT_TRUE(server.Read(&buf));
  EXPECT_EQ(expected_prefix, buf.substr(0, expected_prefix.size()));

  FX_CHECK(server.WriteContent("<!doctype html><img src=\"/img.png\">"));

  expected_prefix = "GET /img.png HTTP";
  buf.resize(expected_prefix.size());
  ASSERT_TRUE(server.Read(&buf));

  ASSERT_GE(buf.size(), expected_prefix.size());
  EXPECT_EQ(expected_prefix, std::string(buf.data(), expected_prefix.size()));
}

class MockNavigationEventListener : public fuchsia::web::NavigationEventListener {
 public:
  // |fuchsia::web::NavigationEventListener|
  void OnNavigationStateChanged(fuchsia::web::NavigationState change,
                                OnNavigationStateChangedCallback callback) override {
    if (on_navigation_state_changed_) {
      on_navigation_state_changed_(std::move(change));
    }

    callback();
  }

  void set_on_navigation_state_changed(fit::function<void(fuchsia::web::NavigationState)> fn) {
    on_navigation_state_changed_ = std::move(fn);
  }

 private:
  fit::function<void(fuchsia::web::NavigationState)> on_navigation_state_changed_;
};

class ChromiumAppTest : public gtest::RealLoopFixture {
 protected:
  ChromiumAppTest()
      : web_context_(sys::ComponentContext::CreateAndServeOutgoingDirectory().get()) {}

  WebContext* web_context() { return &web_context_; }

 private:
  WebContext web_context_;
};

// This test ensures that we can interact with the fuchsia.web FIDL.
//
// See also
// https://chromium.googlesource.com/chromium/src/+/master/fuchsia/engine/browser/context_impl_browsertest.cc
TEST_F(ChromiumAppTest, CreateAndNavigate) {
  MockNavigationEventListener navigation_event_listener;
  fidl::Binding<fuchsia::web::NavigationEventListener> navigation_event_listener_binding(
      &navigation_event_listener);
  web_context()->web_frame()->SetNavigationEventListener(
      navigation_event_listener_binding.NewBinding());
  navigation_event_listener_binding.set_error_handler([](zx_status_t status) {
    FAIL() << "navigation_event_listener_binding: " << zx_status_get_string(status);
  });

  std::string observed_url;
  std::string observed_title;

  // If set, the URL and title events are used to determine if a page has
  // loaded. Otherwise, |is_main_document_loaded| is used instead.
  // TODO(fxbug.dev/29937): Remove this workaround once Chromium has rolled with the new
  // behavior.
  bool use_legacy_observer_behavior = true;

  navigation_event_listener.set_on_navigation_state_changed(
      [this, &navigation_event_listener, &observed_url, &observed_title,
       &use_legacy_observer_behavior](fuchsia::web::NavigationState change) {
        if (change.has_url()) {
          observed_url = change.url();
        }
        if (change.has_title()) {
          observed_title = change.title();
        }

        if (change.has_page_type()) {
          EXPECT_EQ(change.page_type(), fuchsia::web::PageType::NORMAL);
        }

        if (change.has_is_main_document_loaded())
          use_legacy_observer_behavior = false;

        if ((use_legacy_observer_behavior && !(observed_url.empty() || observed_title.empty())) ||
            (!use_legacy_observer_behavior &&
             (change.has_is_main_document_loaded() && change.is_main_document_loaded()))) {
          navigation_event_listener.set_on_navigation_state_changed(nullptr);
          QuitLoop();
        }
      });

  web_runner_tests::TestServer server;
  FX_CHECK(server.FindAndBindPort());

  const std::string url = fxl::StringPrintf("http://localhost:%d/foo.html", server.port());
  web_context()->Navigate(url);

  ASSERT_TRUE(server.Accept());

  const std::string expected_prefix = "GET /foo.html HTTP";
  std::string buf(expected_prefix.size(), 0);
  ASSERT_TRUE(server.Read(&buf));
  EXPECT_EQ(expected_prefix, buf.substr(0, expected_prefix.size()));
  FX_CHECK(server.WriteContent(R"(<!doctype html>
      <html>
        <head>
          <title>Test title!</title>
        </head>
      </html>)"));

  EXPECT_FALSE(RunLoopWithTimeout(zx::sec(5))) << "Timed out waiting for navigation events";

  EXPECT_EQ(url, observed_url);
  EXPECT_EQ("Test title!", observed_title);
}

}  // namespace
