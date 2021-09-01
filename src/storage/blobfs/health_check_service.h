// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_HEALTH_CHECK_SERVICE_H_
#define SRC_STORAGE_BLOBFS_HEALTH_CHECK_SERVICE_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <fidl/fuchsia.update.verify/cpp/wire.h>
#include <lib/async/dispatcher.h>

#include "src/lib/storage/vfs/cpp/service.h"
#include "src/storage/blobfs/blobfs.h"

namespace blobfs {

// HealthCheckService is a service which clients can use to ask blobfs to perform basic self-checks
// of runtime behaviour (such as reading, writing files).
class HealthCheckService : public fidl::WireServer<fuchsia_update_verify::BlobfsVerifier>,
                           public fs::Service {
 public:
  // fuchsia.update.verify.BlobfsVerifier interface
  void Verify(VerifyRequestView request, VerifyCompleter::Sync& completer) final;

 protected:
  friend fbl::internal::MakeRefCountedHelper<HealthCheckService>;
  friend fbl::RefPtr<HealthCheckService>;

  HealthCheckService(async_dispatcher_t* dispatcher, Blobfs& blobfs);

  ~HealthCheckService() override = default;

 private:
  Blobfs& blobfs_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_HEALTH_CHECK_SERVICE_H_
