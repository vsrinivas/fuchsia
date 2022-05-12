// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Creates a trace provider service that enables traces created by a process
/// to be collected by the system trace manager.
///
/// Typically applications would call this method once, early in their main
/// function to enable them to be eligible to produce traces.
///
/// It is safe but unnecessary to call this function more than once.
pub fn trace_provider_create_with_fdio() {
    unsafe {
        sys::trace_provider_create_with_fdio_rust();
    }
}

mod sys {
    #[link(name = "rust-trace-provider")]
    extern "C" {
        // See the C++ documentation for this function in trace_provider.cc
        pub fn trace_provider_create_with_fdio_rust();
    }
}
