// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_NOOP_CLIPBOARD_IMPL_H_
#define SRC_MODULAR_BIN_BASEMGR_NOOP_CLIPBOARD_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

namespace modular {

// An agent responsible for providing the fuchsia::modular::Clipboard service to
// basemgr
class NoopClipboardImpl : public fuchsia::modular::Clipboard {
 public:
  NoopClipboardImpl() {}
  ~NoopClipboardImpl() = default;

  void Connect(fidl::InterfaceRequest<fuchsia::modular::Clipboard> request) {
    bindings_.AddBinding(this, std::move(request));
  }

  // No-op call, implemented for completeness.
  void Push(std::string text) {}

  // |fuchsia::modular::Clipboard|
  void Peek(PeekCallback callback) { callback(""); }

 private:
  // The bindings set containing the outgoing services request from the agent
  // driver.
  fidl::BindingSet<fuchsia::modular::Clipboard> bindings_;
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_NOOP_CLIPBOARD_IMPL_H_
