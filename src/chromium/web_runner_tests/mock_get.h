// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CHROMIUM_WEB_RUNNER_TESTS_MOCK_GET_H_
#define SRC_CHROMIUM_WEB_RUNNER_TESTS_MOCK_GET_H_

#include "src/chromium/web_runner_tests/test_server.h"

namespace web_runner_tests {

// This is a mock GET request handler built on top of |TestServer| that handles serving test pages
// hosted in /pkg/data. Assuming they live in a /data subdirectory of your test, the |test_package|
// target of your BUILD.gn needs a |resources| variable that looks something like this:
//
// resources = [
//   {
//     path = rebase_path("data/my_page.html")
//     dest = "my_page.html"
//   },
// ]
//
// For an example usage, see web_runner_pixel_tests.cc.
void MockHttpGetResponse(web_runner_tests::TestServer* server, const char* resource);

}  // namespace web_runner_tests

#endif  // SRC_CHROMIUM_WEB_RUNNER_TESTS_MOCK_GET_H_
