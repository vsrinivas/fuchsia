// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_DRIVER_HOST_ENV_H_
#define SRC_DEVICES_DRIVER_HOST_ENV_H_

// getenv_bool looks in the environment for |key|. If not found, it
// returns |default_value|. If found, it returns false if the found
// value matches "0", "off", or "false", otherwise it returns true.
bool getenv_bool(const char* key, bool default_value);

#endif  // SRC_DEVICES_DRIVER_HOST_ENV_H_
