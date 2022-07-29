// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// When using cargo, we don't need to have a mod.rs within a tests/ directory, because cargo
// special-cases the tests/ directory for use as integration tests. However, our build system
// doesn't do so.

mod client_e2e;
