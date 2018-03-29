// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/agents/clipboard/clipboard_impl.h"

#include <string>

#include "peridot/lib/fidl/array_to_string.h"

namespace modular {
namespace {
constexpr char kClipboardImplPageId[] = "ClipboardPage___";  // 16 chars

LedgerPageId GetPageId() {
  LedgerPageId id;
  static_assert(sizeof(LedgerPageId) == sizeof(kClipboardImplPageId) - 1,
                "Sizes must match.");
  memcpy(id.mutable_data(), kClipboardImplPageId, id.count());
  return id;
}

}  // namespace

ClipboardImpl::ClipboardImpl(LedgerClient* ledger_client)
    : storage_(ledger_client, GetPageId()) {}

ClipboardImpl::~ClipboardImpl() = default;

void ClipboardImpl::Push(fidl::StringPtr text) {
  storage_.Push(text);
}

void ClipboardImpl::Peek(PeekCallback callback) {
  storage_.Peek(callback);
}

void ClipboardImpl::Connect(fidl::InterfaceRequest<Clipboard> request) {
  bindings_.AddBinding(this, std::move(request));
}

}  // namespace modular
