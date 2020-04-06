// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_UTIL_UTIL_H_
#define SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_UTIL_UTIL_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/vfs/cpp/pseudo_dir.h>

namespace accessibility_test {

constexpr int kMaxLogBufferSize = 1024;

// Utility function to read a file with a vfs::internal::Node.
char *ReadFile(vfs::internal::Node *node, int length, char *buffer);

// Helper function for ReadFile() to Open a File Descriptor.
int OpenAsFD(vfs::internal::Node *node, async_dispatcher_t *dispatcher);

// Create a test node with only a node id and a label.
fuchsia::accessibility::semantics::Node CreateTestNode(
    uint32_t node_id, std::string label, std::vector<uint32_t> child_ids = std::vector<uint32_t>());

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_UTIL_UTIL_H_
