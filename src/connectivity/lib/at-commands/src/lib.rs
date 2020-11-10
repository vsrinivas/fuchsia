// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Build lowlevel types into the library but don't let clients use them.
mod lowlevel;

// Reexport generated high level types for use by clients.
mod generated;
pub use generated::*;
