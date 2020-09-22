// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Separation character for an ACTS command
pub const COMMAND_DELIMITER: &str = ".";

// Number of clauses for an ACTS command
pub const COMMAND_SIZE: usize = 2;

// Maximum number of requests to handle concurrently
// TODO(fxbug.dev/4783) figure out a good parallel value for this
pub const CONCURRENT_REQ_LIMIT: usize = 10;
