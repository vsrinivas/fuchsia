// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/scoped_tmpfs/scoped_tmpfs.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sync/completion.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>

#include "src/lib/fsl/io/fd.h"

namespace scoped_tmpfs {

namespace {

async_loop_config_t MakeConfig() {
  async_loop_config_t result = kAsyncLoopConfigAttachToCurrentThread;
  result.make_default_for_current_thread = false;
  return result;
}

ScopedMemfs MakeMemfs(async_dispatcher_t* dispatcher) {
  zx::result<ScopedMemfs> memfs = ScopedMemfs::Create(dispatcher);
  FX_CHECK(memfs.is_ok());
  memfs->set_cleanup_timeout(zx::sec(10));
  return std::move(*memfs);
}

}  // namespace

ScopedTmpFS::ScopedTmpFS()
    : config_(MakeConfig()), loop_(&config_), memfs_(MakeMemfs(loop_.dispatcher())) {
  zx_status_t status = loop_.StartThread("tmpfs_thread");
  FX_CHECK(status == ZX_OK);

  root_fd_ = fsl::OpenChannelAsFileDescriptor(std::move(memfs_.root()));
  FX_CHECK(root_fd_.is_valid());
}

ScopedTmpFS::~ScopedTmpFS() = default;

}  // namespace scoped_tmpfs
