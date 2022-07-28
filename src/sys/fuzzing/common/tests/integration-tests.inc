// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file allows derived runners to quickly specialize the generic |Runner| integration tests.
// See src/sys/fuzzing/common/tests/integration-tests.h for details.

#ifndef ENGINE_INTEGRATION_TEST
#error "ENGINE_INTEGRATION_TEST must be defined before including this file."
#endif

TEST_F(ENGINE_INTEGRATION_TEST, Crash) { Crash(); }
