// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_INTL_INTL_SERVICES_RUN_H_
#define SRC_INTL_INTL_SERVICES_RUN_H_

#include <zircon/status.h>

namespace intl {

// Runs the server for the `fuchsia.intl.ProfileProvider` service.  The function
// blocks by running the async loop, and returns the status reported by the async
// loop when it exits.
zx_status_t serve_intl_profile_provider(int argc, const char **argv);

}  // namespace intl

#endif  // SRC_INTL_INTL_SERVICES_RUN_H_
