// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_HEALTH_CHECK_SERVICE_H_
#define SRC_STORAGE_BLOBFS_HEALTH_CHECK_SERVICE_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <fuchsia/update/verify/llcpp/fidl.h>
#include <lib/async/dispatcher.h>

#include <fs/service.h>

namespace blobfs {

// HealthCheckService is a service which clients can use to ask blobfs to perform basic
// self-checks of runtime behaviour (such as reading, writing files).
class HealthCheckService : public llcpp::fuchsia::update::verify::BlobfsVerifier::Interface,
                           public fs::Service {
 public:
  explicit HealthCheckService(async_dispatcher_t* dispatcher);

  // fuchsia.update.verify.BlobfsVerifier interface
  void Verify(llcpp::fuchsia::update::verify::VerifyOptions options,
              VerifyCompleter::Sync& completer) final;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_HEALTH_CHECK_SERVICE_H_
