// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/internal/logging.h>
#include <lib/fidl/cpp/internal/message_sender.h>
#include <zircon/fidl.h>

namespace fidl {
namespace internal {

MessageSender::~MessageSender() = default;

zx_status_t SendMessage(const zx::channel& channel, const fidl_type_t* type, Message message) {
  const char* error_msg = nullptr;
  zx_status_t status = message.Validate(type, &error_msg);
  if (status != ZX_OK) {
    FIDL_REPORT_ENCODING_ERROR(message, type, error_msg);
    return status;
  }

  status = message.Write(channel.get(), 0);
  if (status != ZX_OK) {
    FIDL_REPORT_CHANNEL_WRITING_ERROR(message, type, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace internal
}  // namespace fidl
