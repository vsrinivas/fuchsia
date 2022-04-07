// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use syn::{
    spanned::Spanned,
    visit::{self, Visit},
};

use std::{
    borrow::Cow,
    collections::{BTreeMap, HashMap, HashSet},
    fs,
    io::BufRead,
    path::Path,
};

use crate::lint::{filter_lints, Lint};
use crate::owners::get_owners;
use crate::span::Span;

pub fn allow<R: BufRead>(
    lints: &mut R,
    filter: &[String],
    fuchsia_dir: &Path,
    dryrun: bool,
    markdown: bool,
) -> Result<(), Error> {
    println!("Searching for lints: {}\n", filter.join(", "));
    let mut components: HashMap<String, String> = Default::default();
    let mut no_comp: HashMap<String, String> = Default::default();
    let mut no_owners = String::new();
    for (file, lints) in filter_lints(lints, filter) {
        let mut insertions = match annotate(&file, &lints, dryrun, markdown) {
            Ok(ins) => ins,
            Err(e) => {
                eprintln!("Failed to annotate {}: {:?}", file, e);
                continue;
            }
        };
        insertions.sort();
        let msg = if markdown {
            format!("\n[{}]({})\n{}\n", file, codesearch_url(&file, None), insertions.join("\n"))
        } else {
            format!(
                "\t{}\n\t\t{}\n",
                cli_linkify(&file, &codesearch_url(&file, None)),
                insertions.join("\n\t\t")
            )
        };
        let owners = get_owners(Path::new(&file), fuchsia_dir);
        if let Some(comp) = owners.iter().find_map(|o| o.component.clone()) {
            components.entry(comp).or_default().push_str(&msg)
        } else if let Some(owner) = owners.iter().find(|o| !o.users.is_empty()) {
            no_comp.entry(owner.users.join(", ")).or_default().push_str(&msg)
        } else {
            no_owners.push_str(&msg)
        }
    }

    if !components.is_empty() {
        println!("\nComponents:");
        for (c, msg) in components {
            println!("{}\n{}\n", c, msg);
        }
    }
    if !no_comp.is_empty() {
        println!("\nFiles missing Components:");
        for (o, msg) in no_comp {
            println!("{}\n{}\n", o, msg);
        }
    }
    if !no_owners.is_empty() {
        println!("\nFiles missing any OWNERS whatsoever:\n{}", no_owners);
    }
    Ok(())
}

#[derive(Clone, Debug)]
struct Finder {
    /// records the narrowest span encapsulating a given lint
    lints: HashMap<Lint, Span>,
}

// Each lint starts with a maximum span, which the visitor tries to narrow as
// it traverses the file. Narrowing can only occur when:
// "narrowest span so far" ⊇ "span being visited" ⊇ "span of the lint itself"
impl Finder {
    fn narrow(&mut self, potential: Span) {
        for (l, narrowed) in self.lints.iter_mut() {
            if narrowed.contains(potential) && potential.contains(l.span) {
                *narrowed = potential
            }
        }
    }

    // Some lints can't go on certain syntax items (needless_return on
    // statements for example). This doesn't narrow any of the lints listed
    // in the filter
    fn narrow_unless(&mut self, potential: Span, filter: &[&'static str]) {
        for (l, narrowed) in self.lints.iter_mut() {
            if !filter.contains(&l.name.as_str())
                && narrowed.contains(potential)
                && potential.contains(l.span)
            {
                *narrowed = potential
            }
        }
    }
}

// The syntax items of this visitor are the ones on which we'll insert attributes
impl<'ast> Visit<'ast> for Finder {
    fn visit_item(&mut self, i: &'ast syn::Item) {
        self.narrow(i.span().into());
        visit::visit_item(self, i);
    }
    fn visit_member(&mut self, m: &'ast syn::Member) {
        self.narrow(m.span().into());
        visit::visit_member(self, m);
    }
    fn visit_impl_item(&mut self, i: &'ast syn::ImplItem) {
        self.narrow_unless(i.span().into(), &["clippy::serde_api_misuse"]);
        visit::visit_impl_item(self, i);
    }
    fn visit_attribute(&mut self, a: &'ast syn::Attribute) {
        self.narrow(a.span().into());
        visit::visit_attribute(self, a);
    }
    fn visit_stmt(&mut self, s: &'ast syn::Stmt) {
        self.narrow_unless(
            s.span().into(),
            &[
                // these only apply to functions, not statements
                "clippy::needless_return",
                "clippy::not_unsafe_ptr_arg_deref",
                "clippy::unused_io_amount",
                // these are often used on statements which are macro invocations
                "clippy::unit_cmp",
                "clippy::approx_constant",
                "clippy::vtable_address_comparisons",
            ],
        );
        visit::visit_stmt(self, s);
    }
}

/// Returns a message for each lint allowed.
fn annotate(
    filename: &str,
    lints: &[Lint],
    dryrun: bool,
    markdown: bool,
) -> Result<Vec<String>, Error> {
    if !dryrun {
        let src = fs::read_to_string(filename)?;
        let insertions = calculate_insertions(&src, lints)?;
        fs::write(filename, apply_insertions(&src, insertions))?;
    }
    Ok(lints.iter().map(|l| annotation_msg(l, filename, markdown)).collect())
}

fn codesearch_url(filename: &str, line: Option<usize>) -> String {
    let mut link = format!("https://cs.opensource.google/fuchsia/fuchsia/+/main:{}", filename);
    if let Some(line) = line {
        link.push_str(&format!(";l={}", line))
    }
    link
}

fn cli_linkify(text: &str, address: &str) -> String {
    format!("\x1b]8;;{}\x1b\\{}\x1b]8;;\x1b\\", address, text)
}

fn calculate_insertions(
    src: &str,
    lints: &[Lint],
) -> Result<BTreeMap<usize, HashSet<String>>, Error> {
    let mut finder =
        Finder { lints: lints.iter().cloned().map(|l| (l, Span::default())).collect() };
    finder.visit_file(&syn::parse_file(src)?);

    // Group lints by the line where they occur
    let mut inserts: BTreeMap<usize, HashSet<String>> = BTreeMap::new();
    for (lint, smallest) in finder.lints {
        inserts.entry(smallest.start.line).or_default().insert(lint.name);
    }
    Ok(inserts)
}

fn apply_insertions(src: &str, insertions: BTreeMap<usize, HashSet<String>>) -> String {
    let ends_with_newline = src.ends_with('\n');
    let mut lines: Vec<Cow<'_, str>> = src.lines().map(Cow::from).collect();

    // Insert attributes from back to front to avoid disrupting spans
    for (line, lints) in insertions.into_iter().rev() {
        // lines are 1 indexed in diagnostics
        assert_ne!(line, 0, "Didn't narrow span at all");
        let to_annotate = lines[line - 1].to_owned();
        // this should copy the exact whitespace from the line after, including tabs
        let indent = &to_annotate
            [0..to_annotate.find(|c: char| !c.is_whitespace()).unwrap_or(to_annotate.len())];
        let mut lints_for_line: Vec<String> = lints.into_iter().collect();
        lints_for_line.sort();
        lines.insert(
            line - 1,
            format!("{}#[allow({})] // TODO(INSERT_LINT_BUG)", indent, lints_for_line.join(", "))
                .into(),
        );
    }
    (lines.join("\n") + if ends_with_newline { "\n" } else { "" }).to_owned()
}

fn annotation_msg(l: &Lint, filename: &str, markdown: bool) -> String {
    let lints_url = "https://rust-lang.github.io/rust-clippy/master#".to_owned();
    let cs_url = codesearch_url(filename, Some(l.span.start.line));
    let cli_cs_link =
        cli_linkify(&format!("{}:{}", l.span.start.line, l.span.start.column), &cs_url);

    // handle both clippy and normal rustc lints, in either markdown or plaintext
    match (l.name.strip_prefix("clippy::"), markdown) {
        (Some(name), true) => {
            format!(
                "- [{name}]({}) on [line {}]({})",
                &(lints_url + name),
                l.span.start.line,
                cs_url
            )
        }
        (Some(name), false) => format!("{cli_cs_link}\t{}", cli_linkify(name, &(lints_url + name))),
        (None, true) => {
            format!("- {} on [line {}]({})", l.name, l.span.start.line, cs_url)
        }
        (None, false) => format!("{cli_cs_link}\t{}", l.name),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lint::Lint;

    fn lint(name: String, start: (usize, usize), end: (usize, usize)) -> Lint {
        Lint { name, span: crate::span::span(start, end) }
    }

    const BASIC: &'static str = "
fn main() {
    let x = 1;
    let y = 2;
}";

    const NESTED: &'static str = "
impl Foo for Bar {
    type T = Blah;
    fn method(&self, param: A) {
        if condition {
            statement();
            other_statement(|| {
                with_nested();
                let statement = {
                    let x = 42;
                    x+1
                };
            });
        }
    }
}";

    #[test]
    fn test_basic() {
        let name = "LINTNAME".to_owned();
        let insertions =
            calculate_insertions(BASIC, &[lint(name.clone(), (3, 5), (3, 6))]).unwrap();
        assert_eq!(
            apply_insertions(BASIC, insertions),
            "
fn main() {
    #[allow(LINTNAME)] // TODO(INSERT_LINT_BUG)
    let x = 1;
    let y = 2;
}"
        );
    }

    #[test]
    fn test_multiple_lints() {
        let insertions = calculate_insertions(
            BASIC,
            &[
                lint("LINTNAME".to_owned(), (3, 5), (3, 6)),
                lint("OTHERNAME".to_owned(), (3, 5), (3, 6)),
            ],
        )
        .unwrap();
        assert_eq!(
            apply_insertions(BASIC, insertions),
            "
fn main() {
    #[allow(LINTNAME, OTHERNAME)] // TODO(INSERT_LINT_BUG)
    let x = 1;
    let y = 2;
}"
        );
    }

    #[test]
    fn test_multi_line_span() {
        let name = "LINTNAME".to_owned();
        let insertions =
            calculate_insertions(BASIC, &[lint(name.clone(), (2, 4), (3, 6))]).unwrap();
        assert_eq!(
            apply_insertions(BASIC, insertions),
            "
#[allow(LINTNAME)] // TODO(INSERT_LINT_BUG)
fn main() {
    let x = 1;
    let y = 2;
}"
        );
    }

    #[test]
    fn test_nested_scope_statement() {
        let name = "LINTNAME".to_owned();
        let insertions =
            calculate_insertions(NESTED, &[lint(name.clone(), (10, 21), (10, 30))]).unwrap();
        assert_eq!(
            apply_insertions(NESTED, insertions).lines().nth(9).unwrap(),
            "                    #[allow(LINTNAME)] // TODO(INSERT_LINT_BUG)"
        );
    }

    #[test]
    fn test_nested_scope_block() {
        let name = "LINTNAME".to_owned();
        let insertions =
            calculate_insertions(NESTED, &[lint(name.clone(), (12, 17), (12, 18))]).unwrap();
        assert_eq!(
            apply_insertions(NESTED, insertions).lines().nth(8).unwrap(),
            "                #[allow(LINTNAME)] // TODO(INSERT_LINT_BUG)"
        );
    }

    #[test]
    fn test_nested_scope_multi_line() {
        let name = "LINTNAME".to_owned();
        let insertions =
            calculate_insertions(NESTED, &[lint(name.clone(), (6, 13), (7, 30))]).unwrap();
        assert_eq!(
            apply_insertions(NESTED, insertions).lines().nth(4).unwrap(),
            "        #[allow(LINTNAME)] // TODO(INSERT_LINT_BUG)"
        );
    }
}
