// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/bin/start-storage-benchmark/memfs.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/result.h>

#include <memory>

#include "src/storage/memfs/scoped_memfs.h"

namespace storage_benchmark {

zx::result<std::unique_ptr<Memfs>> Memfs::Create() {
  auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigNeverAttachToThread);
  if (zx_status_t status = loop->StartThread(); status != ZX_OK) {
    return zx::error(status);
  }
  auto setup = ScopedMemfs::Create(loop->dispatcher());
  if (setup.is_error()) {
    return setup.take_error();
  }
  return zx::ok(std::unique_ptr<Memfs>(new Memfs(std::move(loop), std::move(*setup))));
}

zx::result<fidl::ClientEnd<fuchsia_io::Directory>> Memfs::GetFilesystemRoot() const {
  return component::Clone(memfs_.root());
}

}  // namespace storage_benchmark
