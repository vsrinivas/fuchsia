// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "env.h"

#include <stdlib.h>
#include <string.h>

bool getenv_bool(const char* key, bool default_value) {
  const char* value = getenv(key);
  if (value == nullptr) {
    return default_value;
  }
  if ((strcmp(value, "0") == 0) || (strcmp(value, "false") == 0) || (strcmp(value, "off") == 0)) {
    return false;
  }
  return true;
}
