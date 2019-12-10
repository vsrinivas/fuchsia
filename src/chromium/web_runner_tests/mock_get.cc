// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/chromium/web_runner_tests/mock_get.h"

#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"

namespace web_runner_tests {

void MockHttpGetResponse(web_runner_tests::TestServer* server, const char* resource) {
  const std::string expected_prefix = fxl::StringPrintf("GET /%s HTTP", resource);
  // |Read| requires preallocate (see sys/socket.h: read)
  std::string buf(expected_prefix.size(), 0);

  EXPECT_TRUE(server->Read(&buf));
  EXPECT_EQ(expected_prefix, buf);
  std::string content;
  FX_CHECK(files::ReadFileToString(fxl::StringPrintf("/pkg/data/%s", resource), &content));
  FX_CHECK(server->WriteContent(content));
}

}  // namespace web_runner_tests
