// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! checker are the traits and structs used to perform checks on markdown documentation
//! in the Fuchsia project.

use {
    crate::md_element::Element,
    anyhow::Result,
    async_trait::async_trait,
    serde_yaml::Value,
    std::{
        fmt::{Debug, Display},
        path::{Path, PathBuf},
    },
};

/// An error reported by a [`DocCheck`].
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DocCheckError {
    pub doc_line: DocLine,
    pub message: String,
}

impl DocCheckError {
    pub fn new(line_num: usize, file_name: PathBuf, message: &str) -> Self {
        DocCheckError { doc_line: DocLine { line_num, file_name }, message: message.to_string() }
    }
}

/// A line within a file.
#[derive(Debug, Clone, Eq, Hash, Ord, PartialOrd, PartialEq)]
pub struct DocLine {
    pub line_num: usize,
    pub file_name: PathBuf,
}

impl Display for DocLine {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_fmt(format_args!("{}:{}", self.file_name.to_string_lossy(), self.line_num))
    }
}

/// Trait for DocCheck. Implementations of this trait are collected into a list
/// and check() is called for each event during parsing the markdown.
#[async_trait]
pub trait DocCheck {
    /// Name for this check. This name is used in error messages to identify the source
    /// of the problem.
    fn name(&self) -> &str;

    /// Given the event, determine if this check applies to the event and return a list
    /// of errors if any are detected.
    /// None should be returned if the check does not apply to this event,
    /// or if no errors are found.
    /// The reference to self is mut, which enables checks to collection information as
    /// each file is checked and then consulted in the post_check.
    fn check(&mut self, element: &Element<'_>) -> Result<Option<Vec<DocCheckError>>>;

    /// Some checks require visiting all pages, the post_check method is called after all
    /// markdown has been parsed.
    async fn post_check(&self) -> Result<Option<Vec<DocCheckError>>>;
}

/// Trait for DocCheck to use with yaml files. Implementations of this trait are collected
/// into a list and check() is called for each event during parsing the yaml.
#[async_trait]
pub trait DocYamlCheck {
    /// Name for this check. This name is used in error messages to identify the source
    /// of the problem.
    fn name(&self) -> &str;

    /// Given the yaml, determine if this check applies to the event and return a list
    /// of errors if any are detected.
    /// An empty list of errors should be returned if the check does not apply to this event,
    /// or if no errors are found.
    fn check(&mut self, filename: &Path, yaml_value: &Value) -> Result<Option<Vec<DocCheckError>>>;

    /// Some checks require visiting all pages, the post_check method is called after all
    /// markdown has been parsed.
    async fn post_check(
        &self,
        markdown_files: &[PathBuf],
        yaml_files: &[PathBuf],
    ) -> Result<Option<Vec<DocCheckError>>>;
}
