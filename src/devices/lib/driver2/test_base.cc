// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/test_base.h"

namespace driver::testing {

zx::status<Namespace> CreateNamespace(fidl::ClientEnd<fuchsia_io::Directory> client_end) {
  fidl::Arena allocator;
  fidl::VectorView<fuchsia_component_runner::wire::ComponentNamespaceEntry> ns_entries(allocator,
                                                                                       1);
  ns_entries[0].Allocate(allocator);
  ns_entries[0].set_path(allocator, "/svc").set_directory(allocator, std::move(client_end));
  return Namespace::Create(ns_entries);
}

}  // namespace driver::testing
