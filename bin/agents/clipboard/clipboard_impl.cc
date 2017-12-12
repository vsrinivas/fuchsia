// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "peridot/bin/agents/clipboard/clipboard_impl.h"

namespace modular {

void ClipboardImpl::Push(const fidl::String& text) {
  current_item_ = text;
}

void ClipboardImpl::Peek(const PeekCallback& callback) {
  callback(current_item_);
}

void ClipboardImpl::Connect(fidl::InterfaceRequest<Clipboard> request) {
  bindings_.AddBinding(this, std::move(request));
}

}  // namespace modular