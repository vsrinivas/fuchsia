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
    let sysroot = if cfg!(test) {
        // TODO(josephry): update this to just use env! after http://fxrev.dev/667908 lands
        option_env!("RUST_SYSROOT").unwrap().to_owned()
    } else {
        format!("prebuilt/third_party/rust/{}-{}", HOST_OS, HOST_ARCH)
    };
    let output = Command::new(format!("{}/bin/clippy-driver", sysroot))
        .arg("--sysroot")
        .arg(&sysroot)
        .arg("-Whelp")
        .output()
        .expect("Couldn't run clippy-driver");
    let stdout = String::from_utf8_lossy(&output.stdout);
    let mut lines = stdout.lines().map(str::trim);
    let parse_categories = |line: &str| {
        if let [category, lints] = line.splitn(2, ' ').collect::<Vec<_>>()[..] {
            (category.to_owned(), lints.split(',').map(|s| s.trim().replace('-', "_")).collect())
        } else {
            panic!("Malformed lint category output")
        }
    };
    lines.find(|s| s.starts_with("Lint groups provided by rustc")).expect(&format!(
        "Couldn't parse clippy-driver help output:\nstdout: {}\nstderr:{}\n",
        stdout,
        &String::from_utf8_lossy(&output.stderr)
    ));
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
    lines.find(|s| s.starts_with("Lint groups provided by plugins")).expect(&format!(
        "Couldn't parse clippy-driver help output:\nstdout: {}\nstderr:{}\n",
        stdout,
        &String::from_utf8_lossy(&output.stderr)
    ));
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

    let mut files: HashMap<String, Vec<Lint>> = HashMap::new();
    serde_json::Deserializer::from_reader(input)
        .into_iter::<Diagnostic>()
        .map(|result| result.expect("parsing diagnostic"))
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
