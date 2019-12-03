// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_SERVICE_CPP_READER_H_
#define LIB_INSPECT_SERVICE_CPP_READER_H_

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <lib/inspect/cpp/hierarchy.h>

namespace inspect {

// Read all of the child names from a TreeNameIterator into a vector.
//
// This function continually calls GetNext on the iterator until all child names have been returned.
//
// Returns a promise for the vector of child names names.
fit::promise<std::vector<std::string>> ReadAllChildNames(
    fuchsia::inspect::TreeNameIteratorPtr iterator);

// Read a full inspect::Hierarchy from a fuchsia.inspect.Tree.
//
// fuchsia.inspect.Tree provides lookup support for Link nodes stored in a hierarchy. This function
// uses the protocol to lookup linked data as needed to create a complete view of the entire tree,
// including dynamically generated subtrees.
//
// Returns a promise for the hierarchy parsed from the Tree.
fit::promise<Hierarchy> ReadFromTree(fuchsia::inspect::TreePtr tree);

}  // namespace inspect

#endif  // LIB_INSPECT_SERVICE_CPP_READER_H_
