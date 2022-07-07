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

#[allow(unused)]
fn get_sysroot() -> String {
    #[cfg(target_arch = "x86_64")]
    const HOST_ARCH: &str = "x64";
    #[cfg(target_arch = "aarch64")]
    const HOST_ARCH: &str = "arm64";
    #[cfg(target_os = "linux")]
    const HOST_OS: &str = "linux";
    #[cfg(target_os = "macos")]
    const HOST_OS: &str = "mac";

    #[cfg(test)]
    let sysroot = env!("RUST_SYSROOT").to_owned();
    #[cfg(not(test))]
    let sysroot = format!("prebuilt/third_party/rust/{}-{}", HOST_OS, HOST_ARCH);

    sysroot
}

/// Returns a mapping of the lint categories (all, style, etc.) to the individual
/// names of the lints they contain by parsing the output of `clippy-driver -Whelp`.
pub fn get_categories() -> HashMap<String, HashSet<String>> {
    let sysroot = get_sysroot();
    let output = Command::new(format!("{}/bin/clippy-driver", sysroot))
        .arg("--sysroot")
        .arg(&sysroot)
        .arg("-Whelp")
        .output()
        .expect("Couldn't run clippy-driver");
    if !output.status.success() {
        panic!("Couldn't run clippy-driver: {:?}", output);
    }
    let stdout = String::from_utf8_lossy(&output.stdout);
    let mut lines = stdout.lines().map(str::trim);
    let parse_categories = |line: &str| {
        if let [category, lints] = line.splitn(2, ' ').collect::<Vec<_>>()[..] {
            (category.to_owned(), lints.split(',').map(|s| s.trim().replace('-', "_")).collect())
        } else {
            panic!("Malformed lint category output")
        }
    };
    let err_msg = format!(
        "Couldn't parse clippy-driver help output:\nstdout: {}\nstderr:{}\n",
        stdout,
        &String::from_utf8_lossy(&output.stderr)
    );
    lines.find(|s| s.starts_with("Lint groups provided by rustc")).expect(&err_msg);
    // Skip the expected header from table
    assert_eq!(
        lines.by_ref().take(4).collect::<Vec<_>>(),
        vec![
            "",
            "name  sub-lints",
            "----  ---------",
            "warnings  all lints that are set to issue warnings"
        ]
    );
    let mut categories = lines
        .by_ref()
        .take_while(|line| !line.is_empty())
        .map(parse_categories)
        .collect::<HashMap<_, _>>();
    lines.find(|s| s.starts_with("Lint groups provided by plugins")).expect(&err_msg);
    categories.extend(lines.skip(1).take_while(|line| !line.is_empty()).map(parse_categories));
    categories
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

    let mut files = HashMap::new();
    serde_json::Deserializer::from_reader(input)
        .into_iter::<Diagnostic>()
        .map(|result| result.expect("parsing diagnostic"))
        .filter_map(|d| d.code.as_ref().map(|c| filter_lints.contains(&c.code).then(|| d.clone())))
        .flatten()
        .for_each(|lint| {
            let span =
                lint.spans.into_iter().find(|s| s.is_primary).expect("no primary span found");
            // ignore stuff in the build directory
            if span.file_name.starts_with("out/") {
                eprintln!("Ignoring file inside build dir: {}", span.file_name);
            } else {
                let lint = Lint { name: lint.code.unwrap().code, span: Span::from(&span) };
                files.entry(span.file_name).or_insert(Vec::new()).push(lint);
            }
        });
    files
}

pub struct LintFile {
    pub path: String,
    pub lints: Vec<Lint>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_rustc_categories() {
        let c = get_categories();
        assert!(c["future-incompatible"].contains("forbidden_lint_groups"));
        assert!(c["rust-2021-compatibility"].contains("non_fmt_panics"));
    }

    #[test]
    fn test_clippy_categories() {
        let c = get_categories();
        assert!(c["clippy::complexity"].contains("clippy::zero_divided_by_zero"));
        assert!(c["clippy::suspicious"].contains("clippy::print_in_format_impl"));
    }
}
