// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/internal/thenable.h>

namespace fidl::internal {

ThenableBase::ThenableBase(ClientBase* client_base, fidl::WriteOptions options)
    : client_base_(client_base), options_(std::move(options)) {
  ZX_ASSERT(client_base);
}

ThenableBase::~ThenableBase() {
  ZX_ASSERT_MSG(!client_base_, "Must call either |Then| or |ThenExactlyOnce|");
}

void ThenableBase::SendTwoWay(fidl::OutgoingMessage& message, ResponseContext* context) {
  ZX_ASSERT_MSG(client_base_, "Cannot call |Then| or |ThenExactlyOnce| multiple times");
  client_base_->SendTwoWay(message, context, std::move(options_));
  client_base_ = nullptr;
}

}  // namespace fidl::internal
