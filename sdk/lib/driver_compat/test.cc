// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/driver_compat/connect.h>
#include <lib/fdio/directory.h>

#include <memory>

#include <fbl/ref_ptr.h>
#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/pseudo_file.h"
#include "src/lib/storage/vfs/cpp/service.h"

TEST(CompatConnectTest, Connection) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto file = fbl::MakeRefCounted<fs::UnbufferedPseudoFile>(
      [](fbl::String* output) { return ZX_OK; }, [](std::string_view input) { return ZX_OK; });

  auto directory = fbl::MakeRefCounted<fs::PseudoDir>();
  directory->AddEntry("one", file);
  directory->AddEntry("two", file);

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_EQ(ZX_OK, endpoints.status_value());

  fs::ManagedVfs vfs(loop.dispatcher());

  vfs.ServeDirectory(directory, std::move(endpoints->server));

  bool callback_complete = false;

  compat::FindDirectoryEntries(
      std::move(endpoints->client), loop.dispatcher(),
      [&callback_complete](zx::result<std::vector<std::string>> entries) mutable {
        callback_complete = true;
        ASSERT_EQ(ZX_OK, entries.status_value());
        ASSERT_EQ(3ul, entries->size());
        ASSERT_EQ(std::string("."), entries.value()[0]);
        ASSERT_EQ(std::string("one"), entries.value()[1]);
        ASSERT_EQ(std::string("two"), entries.value()[2]);
      });
  loop.RunUntilIdle();
  ASSERT_TRUE(callback_complete);

  sync_completion_t shutdown;

  vfs.Shutdown([&shutdown](zx_status_t status) {
    sync_completion_signal(&shutdown);
    ASSERT_EQ(status, ZX_OK);
  });
  loop.RunUntilIdle();
  ASSERT_EQ(sync_completion_wait(&shutdown, zx::duration::infinite().get()), ZX_OK);
}
