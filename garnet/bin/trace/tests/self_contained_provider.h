// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_TESTS_SELF_CONTAINED_PROVIDER_H_
#define GARNET_BIN_TRACE_TESTS_SELF_CONTAINED_PROVIDER_H_

// We need to define _ALL_SOURCE in order to get |thrd_create_with_name()|.
// We don't need it in this file, but self_contained_provider.cc needs it.
// If we don't do this here then we have a subtle header inclusion order
// dependency: If we're included first self_contained_provider.cc won't
// compile. Therefore, we do this here. [Yes, this is what happens when you
// have macros that control what symbols headers provider.]
#ifndef _ALL_SOURCE
#define _ALL_SOURCE  // Enables thrd_create_with_name in <threads.h>.
#endif
#include <threads.h>

bool StartSelfContainedProvider(thrd_t* out_thread);

#endif  // GARNET_BIN_TRACE_TESTS_SELF_CONTAINED_PROVIDER_H_
