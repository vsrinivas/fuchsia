// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string_printf.h>
#include <intel-hda/utils/status.h>
#include <zircon/status.h>

#include <string>
#include <string_view>
#include <variant>

namespace audio::intel_hda {

Status::Status(zx_status_t code, const char* message) : code_(code), message_(message) {}

Status::Status(zx_status_t code, fbl::String message) : code_(code), message_(std::move(message)) {}

fbl::String Status::ToString() const {
  if (message_.empty()) {
    return zx_status_get_string(code_);
  }
  return fbl::StringPrintf("%s (%s)", message_.c_str(), zx_status_get_string(code_));
}

Status PrependMessage(const fbl::String& prefix, const Status& status) {
  if (status.message().empty()) {
    return Status(status.code(),
                  fbl::StringPrintf("%s: %s", prefix.c_str(), zx_status_get_string(status.code())));
  }
  return Status(status.code(),
                fbl::StringPrintf("%s: %s", prefix.c_str(), status.message().c_str()));
}

}  // namespace audio::intel_hda
