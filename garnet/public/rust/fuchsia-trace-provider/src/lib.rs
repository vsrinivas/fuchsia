// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Creates a trace provider service that enables traces created by a process
/// to be collected by the system trace manager.
///
/// Typically applications would call this method early in their main function
/// to enable them to be eligible to produce traces.
pub fn trace_provider_create_with_fdio() {
    unsafe {
        sys::trace_provider_create_with_fdio_rust();
    }
}

// TODO(PT-63): Old version of function, to be deleted after soft-transition
// to |trace_provider_create_with_fdio| has completed.
pub fn trace_provider_create() {
    unsafe {
        sys::trace_provider_create_rust();
    }
}

mod sys {
    #[link(name = "rust-trace-provider")]
    extern "C" {
        pub fn trace_provider_create_with_fdio_rust();
        // TODO(PT-63): Delete when soft-transition completed.
        pub fn trace_provider_create_rust();
    }
}
