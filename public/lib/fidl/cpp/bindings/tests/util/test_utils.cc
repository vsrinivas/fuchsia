// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/tests/util/test_utils.h"

#include <string>

#include <magenta/syscalls.h>

#include "lib/fxl/logging.h"

namespace fidl {
namespace test {

bool WriteTextMessage(const mx::channel& handle,
                      const std::string& text) {
  mx_status_t rv =
      handle.write(0, text.data(), static_cast<uint32_t>(text.size()),
                      nullptr, 0u);
  return rv == MX_OK;
}

bool ReadTextMessage(const mx::channel& handle, std::string* text) {
  mx_status_t rv;
  bool did_wait = false;

  uint32_t num_bytes = 0u;
  uint32_t num_handles = 0u;
  for (;;) {
    rv = handle.read(0, nullptr, 0, &num_bytes, nullptr, 0, &num_handles);
    if (rv == MX_ERR_SHOULD_WAIT) {
      if (did_wait) {
        FXL_DCHECK(false);  // Looping endlessly!?
        return false;
      }
      rv = mx_object_wait_one(handle.get(),
                              MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                              MX_TIME_INFINITE, nullptr);
      if (rv != MX_OK)
        return false;
      did_wait = true;
    } else {
      FXL_DCHECK(!num_handles);
      break;
    }
  }

  text->resize(num_bytes);
  rv = handle.read(0, &text->at(0), num_bytes, &num_bytes,
                   nullptr, num_handles, &num_handles);
  return rv == MX_OK;
}

bool DiscardMessage(const mx::channel& handle) {
  mx_status_t rv = handle.read(MX_CHANNEL_READ_MAY_DISCARD,
                               nullptr, 0, nullptr, nullptr, 0, nullptr);
  return rv == MX_OK;
}

}  // namespace test
}  // namespace fidl
