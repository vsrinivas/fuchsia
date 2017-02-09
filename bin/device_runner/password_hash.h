// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_DEVICE_RUNNER_PASSWORD_HASH_H_
#define APPS_MODULAR_SRC_DEVICE_RUNNER_PASSWORD_HASH_H_

#include "lib/ftl/strings/string_view.h"

namespace modular {
bool HashPassword(const std::string& password,
                  std::string* result,
                  ftl::StringView seed = "");

bool CheckPassword(const std::string& password, const std::string& hash);

}  // namespace modular

#endif  // APPS_MODULAR_SRC_DEVICE_RUNNER_PASSWORD_HASH_H_
