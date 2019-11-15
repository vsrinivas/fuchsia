// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_memory_diff/cpp/cloud_controller_factory.h"

#include <fuchsia/ledger/cloud/test/cpp/fidl.h>
#include <lib/async-testing/test_subloop.h>
#include <lib/fidl/cpp/interface_request.h>

#include "src/ledger/bin/fidl/include/types.h"

extern "C" {
async_test_subloop_t* cloud_provider_memory_diff_new_cloud_controller_factory(zx_handle_t handle,
                                                                              uint64_t seed);
}

namespace cloud_provider {

async_test_subloop_t* NewCloudControllerFactory(
    fidl::InterfaceRequest<CloudControllerFactory> request, uint64_t seed) {
  zx_handle_t handle = request.TakeChannel().release();
  return cloud_provider_memory_diff_new_cloud_controller_factory(handle, seed);
}

}  // namespace cloud_provider
