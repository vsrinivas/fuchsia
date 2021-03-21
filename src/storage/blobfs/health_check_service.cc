// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/health_check_service.h"

#include <fuchsia/update/verify/llcpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>

#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/service.h"
#include "src/storage/blobfs/blob.h"
#include "src/storage/blobfs/cache_node.h"
#include "zircon/errors.h"

namespace fuv = fuchsia_update_verify;

namespace blobfs {

HealthCheckService::HealthCheckService(async_dispatcher_t* dispatcher, Blobfs& blobfs)
    : fs::Service(
          [dispatcher, this](fidl::ServerEnd<fuchsia_update_verify::BlobfsVerifier> server_end) {
            return fidl::BindSingleInFlightOnly(dispatcher, std::move(server_end), this);
          }),
      blobfs_(blobfs) {}

void HealthCheckService::Verify(fuv::wire::VerifyOptions options,
                                VerifyCompleter::Sync& completer) {
  constexpr size_t kMaxBytesToVerify = 1024 * 1024;
  size_t bytes_verified = 0;
  zx_status_t status = blobfs_.Cache().ForAllOpenNodes([&](fbl::RefPtr<CacheNode> node) {
    auto blob = fbl::RefPtr<Blob>::Downcast(std::move(node));
    if (blob->DeletionQueued()) {
      // Skip blobs that are scheduled for deletion.
      return ZX_OK;
    }
    if (zx_status_t status = blob->Verify(); status != ZX_OK) {
      FX_LOGS(ERROR) << "Detected corrupted blob " << blob->MerkleRoot();
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
    bytes_verified += blob->SizeData();
    if (bytes_verified >= kMaxBytesToVerify) {
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  });
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(fuv::wire::VerifyError::INTERNAL);
  }
}

}  // namespace blobfs
