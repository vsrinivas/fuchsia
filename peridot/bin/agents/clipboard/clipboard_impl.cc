// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/agents/clipboard/clipboard_impl.h"

#include <string>

#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/ledger_client/page_id.h"

namespace modular {
namespace {
constexpr char kClipboardImplPageId[] = "ClipboardPage___";  // 16 chars
}  // namespace

ClipboardImpl::ClipboardImpl(LedgerClient* ledger_client)
    : storage_(ledger_client, MakePageId(kClipboardImplPageId)) {}

ClipboardImpl::~ClipboardImpl() = default;

void ClipboardImpl::Push(std::string text) { storage_.Push(text); }

void ClipboardImpl::Peek(PeekCallback callback) { storage_.Peek(std::move(callback)); }

void ClipboardImpl::Connect(fidl::InterfaceRequest<fuchsia::modular::Clipboard> request) {
  bindings_.AddBinding(this, std::move(request));
}

}  // namespace modular
