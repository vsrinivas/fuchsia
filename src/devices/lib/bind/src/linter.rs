// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::UserError;
use crate::parser::bind_library;
use crate::parser::common::CompoundIdentifier;
use std::fmt;
use thiserror::Error;

#[derive(Debug, Error, Clone, PartialEq)]
pub enum LinterError {
    LibraryNameMustNotContainUnderscores(CompoundIdentifier),
}

impl fmt::Display for LinterError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", UserError::from(self.clone()))
    }
}

pub fn lint_library<'a>(library: &bind_library::Ast) -> Result<(), LinterError> {
    if library.name.name.contains("_") {
        return Err(LinterError::LibraryNameMustNotContainUnderscores(library.name.clone()));
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::make_identifier;

    #[test]
    fn library_lint_success() {
        let ast = bind_library::Ast {
            name: make_identifier!["valid", "library", "name"],
            using: vec![],
            declarations: vec![],
        };

        assert_eq!(lint_library(&ast), Ok(()));
    }

    #[test]
    fn library_name_has_underscores() {
        let ast = bind_library::Ast {
            name: make_identifier!["invalid", "library_name"],
            using: vec![],
            declarations: vec![],
        };

        assert_eq!(
            lint_library(&ast),
            Err(LinterError::LibraryNameMustNotContainUnderscores(make_identifier![
                "invalid",
                "library_name"
            ]))
        );
    }
}
