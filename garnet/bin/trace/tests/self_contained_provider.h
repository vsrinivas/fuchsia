// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_TESTS_SELF_CONTAINED_PROVIDER_H_
#define GARNET_BIN_TRACE_TESTS_SELF_CONTAINED_PROVIDER_H_

#include <threads.h>

bool StartSelfContainedProvider(thrd_t* out_thread);

#endif  // GARNET_BIN_TRACE_TESTS_SELF_CONTAINED_PROVIDER_H_
