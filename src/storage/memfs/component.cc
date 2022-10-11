// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>
#include <stdlib.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/storage/memfs/memfs.h"
#include "src/storage/memfs/memfs_config.h"
#include "src/storage/memfs/vnode_dir.h"

int main() {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto structured_config = memfs_config::Config::TakeFromStartupHandle();

  fbl::RefPtr<memfs::VnodeDir> tmp_vnode;
  std::unique_ptr<memfs::Memfs> tmp;
  memfs::Memfs::Options options = {
      .max_file_size = structured_config.max_file_size(),
  };
  zx_status_t status =
      memfs::Memfs::CreateWithOptions(loop.dispatcher(), "<tmp>", options, &tmp, &tmp_vnode);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Memfs::Create failed: " << zx_status_get_string(status);
    return EXIT_FAILURE;
  }

  auto outgoing_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  outgoing_dir->AddEntry("root", tmp_vnode);

  tmp->ServeDirectory(outgoing_dir, fidl::ServerEnd<fuchsia_io::Directory>(
                                        zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST))));

  loop.Run();

  return 0;
}
