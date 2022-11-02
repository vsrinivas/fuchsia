// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver/component/cpp/tests/test_base.h>

namespace driver::testing {

zx::result<Namespace> CreateNamespace(fidl::ClientEnd<fuchsia_io::Directory> client_end) {
  fidl::Arena arena;
  fidl::VectorView<fuchsia_component_runner::wire::ComponentNamespaceEntry> ns_entries(arena, 1);
  ns_entries[0] = fuchsia_component_runner::wire::ComponentNamespaceEntry::Builder(arena)
                      .path("/svc")
                      .directory(std::move(client_end))
                      .Build();
  return Namespace::Create(ns_entries);
}

}  // namespace driver::testing
