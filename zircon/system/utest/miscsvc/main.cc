// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

namespace {

using fuchsia_paver::Paver;

TEST(MiscSvcTest, PaverSvccEnumeratesSuccessfully) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  ASSERT_OK(fdio_service_connect(fidl::DiscoverableProtocolDefaultPath<Paver>, remote.release()));

  zx::channel local2, remote2;
  ASSERT_OK(zx::channel::create(0, &local2, &remote2));

  fidl::WireSyncClient<Paver> paver(std::move(local));
  auto result = paver.FindDataSink(std::move(local2));
  ASSERT_OK(result.status());
}

}  // namespace
