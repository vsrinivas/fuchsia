// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A library which sets the global allocator to the system-provided one.

// Set the global allocator.
//
// This will conflict with any other libraries that set the global allocator,
// and should be used uniquely on Fuchsia.
// In the future, it might be good to break this declaration out into its own
// crate, but for now it's convenient to add it here so that Fuchsia apps
// depending on fuchsia_zircon will use the system allocator.
use std::alloc::System;
#[global_allocator]
static ALLOC: System = System;
