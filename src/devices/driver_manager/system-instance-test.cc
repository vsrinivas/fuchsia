// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-instance.h"

#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl-async/bind.h>

#include <memory>

#include <zxtest/zxtest.h>

namespace {

// Create a subclass to access the protected test-only constructor on SystemInstance.
class SystemInstanceForTest : public SystemInstance {
 public:
  SystemInstanceForTest(fdio_ns_t* default_ns_) : SystemInstance(default_ns_) {}
};

struct Context {
  uint32_t open_flags;
  uint32_t open_count;
  char path[PATH_MAX + 1];
};

static zx_status_t DirectoryOpen(void* ctx, uint32_t flags, uint32_t mode, const char* path_data,
                                 size_t path_size, zx_handle_t object) {
  Context* context = reinterpret_cast<Context*>(ctx);
  context->open_flags = flags;
  context->open_count += 1;
  memcpy(context->path, path_data, path_size);
  context->path[path_size] = '\0';
  // Having this handle still open does not spark joy.  Thank it for its
  // service, and then let it go.
  zx_handle_close(object);
  return ZX_OK;
}

static const fuchsia_io_DirectoryAdmin_ops_t kDirectoryAdminOps = []() {
  fuchsia_io_DirectoryAdmin_ops_t ops;
  ops.Open = DirectoryOpen;
  return ops;
}();

class SystemInstanceFsProvider : public zxtest::Test {
 protected:
  SystemInstanceFsProvider() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    ASSERT_OK(loop_.StartThread());

    // We want to create an alternative namespace for this test to use, to keep
    // this test hermetic wrt. other running tests.
    zx_status_t status = fdio_ns_create(&ns_for_test_);
    ZX_ASSERT_MSG(status == ZX_OK, "devcoordinator: cannot create namespace: %s\n",
                  zx_status_get_string(status));

    // Mock out an object that implements DirectoryOpen and records some state.
    // Bind it to the server handle, and provide that to SystemInstance as the fs_root connection.
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    ASSERT_OK(fidl_bind(loop_.dispatcher(), server.release(),
                        reinterpret_cast<fidl_dispatch_t*>(fuchsia_io_DirectoryAdmin_dispatch),
                        &context_, &kDirectoryAdminOps));
    ASSERT_OK(fdio_ns_bind(ns_for_test_, "/", client.release()));
    under_test_.reset(new SystemInstanceForTest(ns_for_test_));
  }

  void CloneFsAndCheckFlags(const char* path, uint32_t expected_flags) {
    uint32_t starting_open_count = context_.open_count;
    zx::channel fs_connection = under_test_->CloneFs(path);

    // Force a describe call on the target of the Open, to resolve the Open. We expect this to fail
    // because our mock just closes the channel after Open.
    int fd;
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, fdio_fd_create(fs_connection.release(), &fd));
    EXPECT_EQ(starting_open_count + 1, context_.open_count);
    EXPECT_EQ(expected_flags, context_.open_flags);
    EXPECT_STR_EQ(path, context_.path);
  }

 private:
  async::Loop loop_;
  fdio_ns_t* ns_for_test_;
  Context context_;
  std::unique_ptr<SystemInstanceForTest> under_test_;
};

}  // namespace
