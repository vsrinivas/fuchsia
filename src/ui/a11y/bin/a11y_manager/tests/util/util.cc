// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>

#include <gtest/gtest.h>

namespace accessibility_test {

char *ReadFile(vfs::internal::Node *node, int length, char *buffer) {
  EXPECT_LE(length, kMaxLogBufferSize);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("ReadingDebugFile");

  int fd = OpenAsFD(node, loop.dispatcher());
  EXPECT_LE(0, fd);

  memset(buffer, 0, kMaxLogBufferSize);
  EXPECT_EQ(length, pread(fd, buffer, length, 0));
  return buffer;
}

int OpenAsFD(vfs::internal::Node *node, async_dispatcher_t *dispatcher) {
  zx::channel local, remote;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &local, &remote));
  EXPECT_EQ(ZX_OK, node->Serve(fuchsia::io::OPEN_RIGHT_READABLE, std::move(remote), dispatcher));
  int fd = -1;
  EXPECT_EQ(ZX_OK, fdio_fd_create(local.release(), &fd));
  return fd;
}

fuchsia::accessibility::semantics::Node CreateTestNode(uint32_t node_id, std::string label,
                                                       std::vector<uint32_t> child_ids) {
  fuchsia::accessibility::semantics::Node node = fuchsia::accessibility::semantics::Node();
  node.set_node_id(node_id);
  if (!child_ids.empty()) {
    node.set_child_ids(std::move(child_ids));
  }
  node.set_role(fuchsia::accessibility::semantics::Role::UNKNOWN);
  node.set_attributes(fuchsia::accessibility::semantics::Attributes());
  node.mutable_attributes()->set_label(std::move(label));
  fuchsia::ui::gfx::BoundingBox box;
  node.set_location(box);
  return node;
}

}  // namespace accessibility_test
