// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CHROMIUM_WEB_RUNNER_TESTS_WEB_CONTEXT_H_
#define SRC_CHROMIUM_WEB_RUNNER_TESTS_WEB_CONTEXT_H_

#include <fuchsia/web/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

// This sub-fixture uses fuchsia.web FIDL services to interact with Chromium.
//
// See also:
// https://chromium.googlesource.com/chromium/src/+/HEAD/fuchsia/engine/test/web_engine_browser_test.h
class WebContext {
 public:
  WebContext(sys::ComponentContext* component_context, fuchsia::web::ContextFeatureFlags flags);
  void Navigate(const std::string& url);

  fuchsia::web::Frame* web_frame() const { return web_frame_.get(); }

 private:
  // This has to stay open while we're interacting with Chromium.
  fuchsia::web::ContextPtr web_context_;

  fuchsia::web::FramePtr web_frame_;
};

#endif  // SRC_CHROMIUM_WEB_RUNNER_TESTS_WEB_CONTEXT_H_
