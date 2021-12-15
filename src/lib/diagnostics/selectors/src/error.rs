// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::types::{Identifier, OneOrMany, Operator, ValueType};
use nom::error::ErrorKind;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum Error {
    #[error("Static selector directories are expected to be flat")]
    NonFlatDirectory,

    #[error(transparent)]
    Parse(#[from] ParseError),

    #[error(transparent)]
    Io(#[from] std::io::Error),

    #[error("Selector arguments must be structured or raw")]
    InvalidSelectorArgument,

    #[error("Property selectors must have non-empty node_path vector")]
    EmptyPropertySelectorNodePath,

    #[error("TreeSelector only supports property and subtree selection.")]
    InvalidTreeSelector,

    #[error("Recursive wildcards aren't allowed in this position")]
    RecursiveWildcardNotAllowed,

    #[error("Selecter fails verification due to unmatched escape character")]
    UnmatchedEscapeCharacter,

    #[error(transparent)]
    Validation(#[from] ValidationError),
}

#[derive(Debug, Error)]
pub enum ParseError {
    #[error("Failed to parse the input. Failed at: {0:?} with: {1:?}")]
    Fast(String, ErrorKind),

    #[error("Failed to parse the input. Error: {0}")]
    Verbose(String),

    #[error(transparent)]
    Validation(#[from] ValidationError),
}

#[derive(Debug, Error)]
pub enum ValidationError {
    #[error("Component selectors require at least one segment")]
    EmptyComponentSelector,

    #[error("Subtree selectors must have non-empty node_path vector")]
    EmptySubtreeSelector,

    #[error("String selectors must be string patterns or exact matches")]
    InvalidStringSelector,

    #[error("String pattern '{0}' failed verification. Errors: {1:?}")]
    InvalidStringPattern(String, Vec<StringPatternError>),

    #[error("Selectors require a tree selector")]
    MissingTreeSelector,

    #[error("Selectors require a component selector")]
    MissingComponentSelector,

    #[error("String patterns cannot be empty.")]
    EmptyStringPattern,

    #[error("Operator {1:?} cannot be used with identifier {0:?}")]
    InvalidOperator(Identifier, Operator),

    #[error("Value {1:?} cannot be used with identifier {0:?}")]
    InvalidValueType(Identifier, OneOrMany<ValueType>),

    #[error("Value {1:?} cannot be used with operator {0:?}")]
    InvalidOperatorRhs(Operator, OneOrMany<ValueType>),
}

#[derive(Debug)]
pub enum StringPatternError {
    UnescapedGlob,
    UnescapedColon,
    UnescapedForwardSlash,
}
