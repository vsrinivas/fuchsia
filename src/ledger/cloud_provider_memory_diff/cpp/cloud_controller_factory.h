// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_MEMORY_DIFF_CPP_CLOUD_CONTROLLER_FACTORY_H_
#define SRC_LEDGER_CLOUD_PROVIDER_MEMORY_DIFF_CPP_CLOUD_CONTROLLER_FACTORY_H_

#include <fuchsia/ledger/cloud/test/cpp/fidl.h>
#include <lib/async-testing/test_subloop.h>

#include "src/ledger/bin/fidl/include/types.h"

namespace cloud_provider {

// Creates a new CloudControllerFactory that runs on the returned subloop. The random number
// generator of the factory is deterministically seeded with |seed|.
async_test_subloop_t* NewCloudControllerFactory(
    fidl::InterfaceRequest<CloudControllerFactory> request, uint64_t seed);

}  // namespace cloud_provider

#endif  // SRC_LEDGER_CLOUD_PROVIDER_MEMORY_DIFF_CPP_CLOUD_CONTROLLER_FACTORY_H_
