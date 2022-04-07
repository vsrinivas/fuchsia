// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use rustfix::diagnostics::Diagnostic;

use std::{
    collections::{HashMap, HashSet},
    io::BufRead,
    process::Command,
};

use crate::span::Span;

#[derive(Debug, Clone, Hash, PartialEq, Eq)]
pub struct Lint {
    /// The lint name such as clippy::needless_borrow or unused_variable
    pub name: String,
    pub span: Span,
}

#[cfg(target_arch = "x86_64")]
const HOST_ARCH: &str = "x64";
#[cfg(target_arch = "aarch64")]
const HOST_ARCH: &str = "arm64";
#[cfg(target_os = "linux")]
const HOST_OS: &str = "linux";
#[cfg(target_os = "macos")]
const HOST_OS: &str = "mac";

/// Returns a mapping of the lint categories (all, style, etc.) to the individual
/// names of the lints they contain by parsing the output of `clippy-driver -Whelp`.
pub fn get_categories() -> HashMap<String, HashSet<String>> {
    let output = Command::new(format!(
        "prebuilt/third_party/rust/{}-{}/bin/clippy-driver",
        HOST_OS, HOST_ARCH
    ))
    .arg("-Whelp")
    .output()
    .expect("Couldn't run clippy-driver");
    let output = String::from_utf8_lossy(&output.stdout);
    let mut lines = output.lines().map(str::trim).filter(|l| !l.is_empty());
    lines
        .find(|s| !s.starts_with("Lint groups provided by plugins"))
        .expect("Couldn't parse clippy-driver help output");
    lines
        .skip(1)
        .map(|line| {
            if let [category, lints] = line.splitn(2, ' ').collect::<Vec<_>>()[..] {
                (
                    category.to_owned(),
                    lints.split(',').map(|s| s.trim().replace('-', "_")).collect(),
                )
            } else {
                panic!("Malformed lint category output")
            }
        })
        .collect()
}

/// Constructs a map of source files to lints contained in them, filtering on
/// the given list of lints and categories.
pub fn filter_lints<R: BufRead>(input: &mut R, filter: &[String]) -> HashMap<String, Vec<Lint>> {
    let categories = get_categories();

    // If a lint category is given, add all lints in that category to the filter
    let mut filter_lints: HashSet<String> = HashSet::new();
    for f in filter {
        if let Some(lints) = categories.get(f) {
            filter_lints.extend(lints.iter().cloned());
        } else {
            filter_lints.insert(f.to_owned());
        }
    }

    let mut files: HashMap<String, Vec<Lint>> = HashMap::new();
    serde_json::Deserializer::from_reader(input)
        .into_iter::<Diagnostic>()
        .filter_map(Result::ok)
        .filter_map(|d| d.code.as_ref().map(|c| filter_lints.contains(&c.code).then(|| d.clone())))
        .flatten()
        .for_each(|lint| {
            // The primary span is always last in the list
            // TODO(josephry): Once rustfix 0.6.1 releases, use Diagnostic::is_primary
            let span = lint.spans.last().expect("no spans found");
            let file = &span.file_name;
            // ignore stuff in the build directory
            if file.starts_with("out/") {
                eprintln!("Ignoring file inside build dir: {}", span.file_name);
            } else {
                files
                    .entry(file.to_string())
                    .or_default()
                    .push(Lint { name: lint.code.unwrap().code, span: span.into() });
            }
        });
    files
}
