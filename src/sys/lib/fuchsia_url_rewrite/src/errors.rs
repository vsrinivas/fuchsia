// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[allow(missing_docs)]
#[derive(Error, Debug, PartialEq, Eq)]
pub enum RuleParseError {
    #[error("invalid hostname")]
    InvalidHost,

    #[error("paths must start with '/'")]
    InvalidPath,

    #[error("paths should both be a prefix match or both be a literal match")]
    InconsistentPaths,
}

#[allow(missing_docs)]
#[derive(Error, Debug, PartialEq, Eq)]
pub enum RuleDecodeError {
    #[error("unknown variant")]
    UnknownVariant,

    #[error("parse error: {}", _0)]
    ParseError(RuleParseError),
}

impl From<RuleParseError> for RuleDecodeError {
    fn from(x: RuleParseError) -> Self {
        RuleDecodeError::ParseError(x)
    }
}
