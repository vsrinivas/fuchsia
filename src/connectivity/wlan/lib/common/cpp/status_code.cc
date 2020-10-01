// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/status_code.h>

#define X(STATUS, _CODE) \
  case STATUS:           \
    return #STATUS;
const char* wlan_status_code_str(wlan_status_code_t status) {
  switch (status) { WLAN_STATUS_CODE_LIST };
  return "(unknown)";
}
#undef X
