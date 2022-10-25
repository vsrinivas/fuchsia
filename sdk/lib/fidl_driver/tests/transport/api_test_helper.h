// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_TESTS_TRANSPORT_API_TEST_HELPER_H_
#define LIB_FIDL_DRIVER_TESTS_TRANSPORT_API_TEST_HELPER_H_

#include <lib/async/cpp/task.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/assert.h>

#include <utility>

#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"

#ifdef NDEBUG
#define DEBUG_ONLY_TEST_MAY_SKIP() GTEST_SKIP() << "Skipped in release build"
#else
#define DEBUG_ONLY_TEST_MAY_SKIP() (void)0
#endif

inline std::pair<fdf::Dispatcher, std::shared_ptr<libsync::Completion>> CreateSyncDispatcher() {
  // Use |FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS| to encourage the driver dispatcher
  // to spawn more threads to back the same synchronized dispatcher.
  constexpr uint32_t kSyncDispatcherOptions = FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS;
  std::shared_ptr<libsync::Completion> dispatcher_shutdown =
      std::make_shared<libsync::Completion>();
  zx::result dispatcher = fdf::Dispatcher::Create(
      kSyncDispatcherOptions, "",
      [dispatcher_shutdown](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown->Signal(); });
  ZX_ASSERT(dispatcher.status_value() == ZX_OK);
  return std::make_pair(std::move(*dispatcher), std::move(dispatcher_shutdown));
}

#endif  // LIB_FIDL_DRIVER_TESTS_TRANSPORT_API_TEST_HELPER_H_
