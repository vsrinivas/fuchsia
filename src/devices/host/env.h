// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_HOST_ENV_H_
#define SRC_DEVICES_HOST_ENV_H_

namespace devmgr {

// getenv_bool looks in the environment for |key|. If not found, it
// returns |default_value|. If found, it returns false if the found
// value matches "0", "off", or "false", otherwise it returns true.
bool getenv_bool(const char* key, bool default_value);

}  // namespace devmgr

#endif  // SRC_DEVICES_HOST_ENV_H_
