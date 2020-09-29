// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FIDL_TEST_LAUNCHER_H_
#define EXAMPLES_FIDL_TEST_LAUNCHER_H_

#include <string>
#include <vector>

// Launch the client and server components such that the client has the specified
// capabilities provided by the server included in its launch environment. See Usage() in main.cc.
int64_t LaunchComponents(std::string client_url, std::string server_url,
                         std::vector<std::string> capability_names);

#endif  // EXAMPLES_FIDL_TEST_LAUNCHER_H_
