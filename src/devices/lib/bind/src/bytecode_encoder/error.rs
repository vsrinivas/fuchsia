// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::UserError;
use crate::parser::bind_library;
use std::fmt;
use thiserror::Error;

#[derive(Debug, Error, Clone, PartialEq)]
pub enum BindRulesEncodeError {
    InvalidStringLength(String),
    UnsupportedSymbol,
    IntegerOutOfRange,
    MismatchValueTypes(bind_library::ValueType, bind_library::ValueType),
    IncorrectTypesInValueComparison,
    DuplicateLabel(u32),
    MissingLabel(u32),
    InvalidGotoLocation(u32),
    JumpOffsetOutOfRange(u32),
    MatchNotSupported,
    MissingCompositeDeviceName,
    MissingCompositeNodeName,
    DuplicateCompositeNodeName(String),
}

impl fmt::Display for BindRulesEncodeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", UserError::from(self.clone()))
    }
}
