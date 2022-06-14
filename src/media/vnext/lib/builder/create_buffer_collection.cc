// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/builder/create_buffer_collection.h"

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

namespace fmlib {

std::pair<zx::eventpair, zx::eventpair> CreateBufferCollection(
    fuchsia::media2::BufferProvider& buffer_provider) {
  zx::eventpair provider_token;
  zx::eventpair participant_token;

  zx_status_t status = zx::eventpair::create(0, &provider_token, &participant_token);
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Failed to create eventpair";
    return std::make_pair(zx::eventpair(), zx::eventpair());
  }

  zx::eventpair first_participant_token;
  status =
      participant_token.duplicate(ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER, &first_participant_token);
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Failed to duplicate eventpair";
    return std::make_pair(zx::eventpair(), zx::eventpair());
  }

  zx::eventpair second_participant_token;
  status = participant_token.duplicate(ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER,
                                       &second_participant_token);
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Failed to duplicate eventpair";
    return std::make_pair(zx::eventpair(), zx::eventpair());
  }

  buffer_provider.CreateBufferCollection(
      std::move(provider_token), "graph",
      [](fuchsia::media2::BufferProvider_CreateBufferCollection_Result result) mutable {
        if (result.is_response()) {
          FX_LOGS(INFO) << "CreateBufferCollection: "
                        << result.response().collection_info.buffer_count() << " buffers of "
                        << result.response().collection_info.buffer_size() << " bytes each";
        } else {
          FX_LOGS(ERROR) << "CreateBufferCollection: failed "
                         << static_cast<uint32_t>(result.err());
        }
      });

  return std::make_pair(std::move(first_participant_token), std::move(second_participant_token));
}

}  // namespace fmlib
