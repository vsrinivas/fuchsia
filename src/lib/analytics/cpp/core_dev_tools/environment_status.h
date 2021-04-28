// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ENVIRONMENT_STATUS_H_
#define SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ENVIRONMENT_STATUS_H_

namespace analytics::core_dev_tools {

bool IsRunByBot();

struct BotInfo {
  const char* const environment = nullptr;
  const char* const name = nullptr;

  bool IsRunByBot() const;
};

// Returns information of the CI bot. When IsRunByBot() returns false, it returns {nullptr,
// nullptr, nullptr}
BotInfo GetBotInfo();

}  // namespace analytics::core_dev_tools

#endif  // SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ENVIRONMENT_STATUS_H_
