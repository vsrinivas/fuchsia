// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/integration/test.h"

#include "apps/maxwell/interfaces/context_engine.mojom.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/bindings/formatting.h"
#include "mojo/public/cpp/bindings/binding.h"

using namespace maxwell::context_engine;
using namespace mojo;

void TestContextEngineLifecycle(Shell* shell) {
  SuggestionAgentClientPtr ptr;
  ConnectToService(shell, "mojo:context_engine", GetProxy(&ptr));
}
