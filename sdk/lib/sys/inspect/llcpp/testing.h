// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_INSPECT_LLCPP_TESTING_H_
#define LIB_SYS_INSPECT_LLCPP_TESTING_H_

#include <fidl/fuchsia.inspect/cpp/wire.h>
#include <lib/fpromise/promise.h>
#include <lib/inspect/cpp/hierarchy.h>

#include <vector>

namespace inspect {
namespace testing {

using TreeNameIteratorClient = fidl::WireClient<fuchsia_inspect::TreeNameIterator>;
using TreeClient = fidl::WireClient<fuchsia_inspect::Tree>;

// Collect all of the names of the children of the `Inspector` managed by the tree server
// which started the TreeNameIterator server. Essentially, it drains the iterator.
fpromise::promise<std::vector<std::string>> ReadAllChildNames(TreeNameIteratorClient& iter);

// Turn a Tree handle into a complete hierarchy, including lazy children/values.
//
// This function uses `ZX_ASSERT` if it encounters FIDL errors.
fpromise::promise<Hierarchy> ReadFromTree(TreeClient& tree, async_dispatcher_t* dispatcher);
}  // namespace testing
}  // namespace inspect

#endif  // LIB_SYS_INSPECT_LLCPP_TESTING_H_
