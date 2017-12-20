// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_AGENTS_CLIPBOARD_CLIPBOARD_AGENT_H_
#define PERIDOT_BIN_AGENTS_CLIPBOARD_CLIPBOARD_AGENT_H_

#include <string>

#include "lib/clipboard/fidl/clipboard.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

namespace modular {

// An agent responsible for providing the Clipboard service.
class ClipboardImpl : Clipboard {
 public:
  ClipboardImpl();

  void Connect(fidl::InterfaceRequest<Clipboard> request);

 private:
  // |Clipboard|
  void Push(const fidl::String& text) override;

  // |Clipboard|
  void Peek(const PeekCallback& callback) override;

  // The current clipboard text.
  std::string current_item_;

  // The bindings set containing the outgoing services request from the agent
  // driver.
  fidl::BindingSet<Clipboard> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ClipboardImpl);
  friend class ClipboardImplTest;
};

}  // namespace modular

#endif  // PERIDOT_BIN_AGENTS_CLIPBOARD_CLIPBOARD_AGENT_H_