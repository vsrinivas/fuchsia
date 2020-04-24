// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_TESTS_COMPONENT_CONTEXT_H_
#define GARNET_BIN_TRACE_TESTS_COMPONENT_CONTEXT_H_

#include <lib/sys/cpp/component_context.h>

#include <memory>

namespace tracing {
namespace test {

// Call this once from main().
void InitComponentContext();

// Call this to get the component context pointer.
// N.B. Use of this value requires the presence of the default async-loop dispatcher.
// This constraint is imposed on us by the use of
// |sys::ComponentContext::CreateAndServeOutgoingDirectory()|.
sys::ComponentContext* GetComponentContext();

}  // namespace test
}  // namespace tracing

#endif  // GARNET_BIN_TRACE_TESTS_COMPONENT_CONTEXT_H_
