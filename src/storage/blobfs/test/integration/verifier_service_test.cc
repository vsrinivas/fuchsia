// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.update.verify/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <string>

#include <gtest/gtest.h>

#include "src/storage/blobfs/test/integration/blobfs_fixtures.h"

namespace blobfs {
namespace {

namespace fuv = fuchsia_update_verify;

class VerifierServiceTest : public BlobfsTest {
 protected:
  fidl::WireSyncClient<fuv::BlobfsVerifier> ConnectToHealthCheckService() {
    auto client_end = component::ConnectAt<fuv::BlobfsVerifier>(fs().ServiceDirectory());
    EXPECT_EQ(client_end.status_value(), ZX_OK);
    return fidl::WireSyncClient<fuv::BlobfsVerifier>(std::move(*client_end));
  }
};

// This test mainly exists to ensure that the service is exported correctly. The business logic is
// exercised by other unit tests.
TEST_F(VerifierServiceTest, EmptyFilesystemIsValid) {
  fuv::wire::VerifyOptions options;
  auto status = ConnectToHealthCheckService()->Verify(std::move(options));
  ASSERT_EQ(status.status(), ZX_OK) << status.error();
}

}  // namespace
}  // namespace blobfs
