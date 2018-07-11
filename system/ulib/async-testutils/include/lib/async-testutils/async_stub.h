// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(davemoore): ZX-2337 Remove after all external references have been changed
// to async_dispatcher_t.
#pragma once

#include <lib/async-testutils/dispatcher_stub.h>

#define AsyncStub DispatcherStub