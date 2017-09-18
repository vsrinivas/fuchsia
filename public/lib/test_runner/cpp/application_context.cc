// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/test_runner/lib/application_context.h"

#include <mutex>

namespace test_runner {
namespace {

std::unique_ptr<app::ApplicationContext> g_app_context;
std::mutex g_mutex;

}  // namespace

app::ApplicationContext* GetApplicationContext() {
  std::lock_guard<std::mutex> guard(g_mutex);

  if (!g_app_context) {
    g_app_context = app::ApplicationContext::CreateFromStartupInfoNotChecked();
  }
  return g_app_context.get();
}

}  // namespace test_runner
