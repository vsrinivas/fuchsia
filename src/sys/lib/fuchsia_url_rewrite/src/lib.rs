// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! Tools for handling Fuchsia URLs.

mod rule;
pub use crate::rule::{Rule, RuleConfig, RuleInspectState};

mod errors;
pub use crate::errors::{RuleDecodeError, RuleParseError};
