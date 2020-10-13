// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/cksum.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>

#include <zxtest/zxtest.h>

#include "src/storage/lib/paver/abr-client.h"
#include "src/storage/lib/paver/astro.h"
#include "src/storage/lib/paver/luis.h"
#include "src/storage/lib/paver/sherlock.h"
#include "src/storage/lib/paver/x64.h"

namespace {

using devmgr_integration_test::RecursiveWaitForFile;
using driver_integration_test::IsolatedDevmgr;

TEST(AstroAbrTests, CreateFails) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.driver_search_paths.push_back("/boot/driver");
  args.disable_block_watcher = false;
  args.board_name = "sherlock";

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));
  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));

  zx::channel svc_root;
  ASSERT_NOT_OK(
      paver::AstroAbrClientFactory().New(devmgr.devfs_root().duplicate(), svc_root, nullptr));
}

TEST(SherlockAbrTests, CreateFails) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.driver_search_paths.push_back("/boot/driver");
  args.disable_block_watcher = false;
  args.board_name = "astro";

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));
  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));

  zx::channel svc_root, remote;
  ASSERT_OK(zx::channel::create(0, &svc_root, &remote));
  ASSERT_OK(fdio_service_connect_at(devmgr.fshost_outgoing_dir().get(), "svc", remote.release()));

  ASSERT_NOT_OK(
      paver::SherlockAbrClientFactory().Create(devmgr.devfs_root().duplicate(), svc_root, nullptr));
}

TEST(LuisAbrTests, CreateFails) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.driver_search_paths.push_back("/boot/driver");
  args.disable_block_watcher = false;
  args.board_name = "astro";

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));
  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));

  zx::channel svc_root, remote;
  ASSERT_OK(zx::channel::create(0, &svc_root, &remote));
  ASSERT_OK(fdio_service_connect_at(devmgr.fshost_outgoing_dir().get(), "svc", remote.release()));

  ASSERT_NOT_OK(
      paver::LuisAbrClientFactory().Create(devmgr.devfs_root().duplicate(), svc_root, nullptr));
}

TEST(X64AbrTests, CreateFails) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.driver_search_paths.push_back("/boot/driver");
  args.disable_block_watcher = false;
  args.board_name = "x64";

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));
  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));

  zx::channel svc_root, remote;
  ASSERT_OK(zx::channel::create(0, &svc_root, &remote));
  ASSERT_OK(fdio_service_connect_at(devmgr.fshost_outgoing_dir().get(), "svc", remote.release()));

  ASSERT_NOT_OK(
      paver::X64AbrClientFactory().Create(devmgr.devfs_root().duplicate(), svc_root, nullptr));
}

}  // namespace
