// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/zx/vmo.h>
#include <zircon/device/vfs.h>

#include <array>
#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/integration/blobfs_fixtures.h"
#include "src/storage/fvm/format.h"
#include "src/storage/lib/utils/topological_path.h"

namespace blobfs {
namespace {

namespace fio = fuchsia_io;
namespace fuchsia_fs = fuchsia_fs;

class QueryServiceTest : public BlobfsWithFvmTest {
 protected:
  fidl::WireSyncClient<fuchsia_fs::Query> ConnectToQueryService() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_fs::Query>();
    EXPECT_EQ(endpoints.status_value(), ZX_OK);
    auto [query_client_end, query_server_end] = *std::move(endpoints);

    std::string query_service_path =
        std::string("svc/") + fidl::DiscoverableProtocolName<fuchsia_fs::Query>;
    EXPECT_EQ(
        fdio_service_connect_at(fs().GetOutgoingDirectory()->get(), query_service_path.c_str(),
                                query_server_end.TakeChannel().release()),
        ZX_OK);
    return fidl::WireSyncClient<fuchsia_fs::Query>(std::move(query_client_end));
  }
};

TEST_F(QueryServiceTest, IsNodeInFilesystemPositiveCase) {
  // Get a token corresponding to the root directory.
  fdio_cpp::UnownedFdioCaller caller(root_fd());
  auto token_result =
      fidl::WireCall(fidl::UnownedClientEnd<fio::Directory>(caller.channel()))->GetToken();
  ASSERT_EQ(token_result.status(), ZX_OK);
  ASSERT_EQ(token_result->s, ZX_OK);
  zx::handle token_raw = std::move(token_result->token);
  ASSERT_TRUE(token_raw.is_valid());
  zx::event token(std::move(token_raw));

  // This token is in the filesystem.
  fidl::WireSyncClient<fuchsia_fs::Query> query_service = ConnectToQueryService();
  auto result = query_service->IsNodeInFilesystem(std::move(token));
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_TRUE(result->is_in_filesystem);
}

TEST_F(QueryServiceTest, IsNodeInFilesystemNegativeCase) {
  // Create some arbitrary event, to fake a token.
  zx::event token;
  zx::event::create(0, &token);

  // This token should not be in the filesystem.
  fidl::WireSyncClient<fuchsia_fs::Query> query_service = ConnectToQueryService();
  auto result = query_service->IsNodeInFilesystem(std::move(token));
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_FALSE(result->is_in_filesystem);
}

}  // namespace
}  // namespace blobfs
