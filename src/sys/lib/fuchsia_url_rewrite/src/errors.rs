// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Fail;

#[derive(Fail, Debug, PartialEq, Eq)]
pub enum RuleParseError {
    #[fail(display = "invalid hostname")]
    InvalidHost,

    #[fail(display = "paths must start with '/'")]
    InvalidPath,

    #[fail(display = "paths should both be a prefix match or both be a literal match")]
    InconsistentPaths,
}

#[derive(Fail, Debug, PartialEq, Eq)]
pub enum RuleDecodeError {
    #[fail(display = "unknown variant")]
    UnknownVariant,

    #[fail(display = "parse error: {}", _0)]
    ParseError(#[cause] RuleParseError),
}

impl From<RuleParseError> for RuleDecodeError {
    fn from(x: RuleParseError) -> Self {
        RuleDecodeError::ParseError(x)
    }
}
