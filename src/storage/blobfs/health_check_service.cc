// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/health_check_service.h"

#include <fuchsia/update/verify/llcpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/channel.h>

#include "src/lib/storage/vfs/cpp/service.h"

namespace fuv = ::llcpp::fuchsia::update::verify;

namespace blobfs {

HealthCheckService::HealthCheckService(async_dispatcher_t* dispatcher)
    : fs::Service([dispatcher, this](
                      fidl::ServerEnd<llcpp::fuchsia::update::verify::BlobfsVerifier> server_end) {
        return fidl::BindSingleInFlightOnly(dispatcher, std::move(server_end), this);
      }) {}

void HealthCheckService::Verify(fuv::VerifyOptions options, VerifyCompleter::Sync& completer) {
  // TODO(fxbug.dev/64608): Implement this.
  completer.ReplySuccess();
}

}  // namespace blobfs
