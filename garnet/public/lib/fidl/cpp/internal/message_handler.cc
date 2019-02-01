// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/message_handler.h"

namespace fidl {
namespace internal {

MessageHandler::~MessageHandler() = default;

void MessageHandler::OnChannelGone() {}

}  // namespace internal
}  // namespace fidl
