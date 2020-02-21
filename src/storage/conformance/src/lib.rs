// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! Crate to provide fidl logging and test setup helpers for conformance tests
//! for io.fidl.

/// Allows logging of requests from a io.Directory channel.
pub mod directory_request_logger;

/// Injector for child components started by component manager BlackBoxTest
/// to connect and provide Io1TestHarness to the test.
pub mod io1_harness_receiver;

/// Allows aggregating requests from many io1 protocol channel loggers into one
/// source.
pub mod io1_request_logger_factory;
