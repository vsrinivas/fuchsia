// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
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

use crate::{
    issues::IssueTemplate,
    lint::{filter_lints, Lint, LintFile},
    monorail::Monorail,
    owners::FileOwnership,
    span::Span,
};

pub fn allow(
    lints: &mut impl BufRead,
    filter: &[String],
    fuchsia_dir: &Path,
    monorail: &mut (impl Monorail + ?Sized),
    issue_template: &mut IssueTemplate<'_>,
    rollout_path: &Path,
    dryrun: bool,
    verbose: bool,
) -> Result<()> {
    println!("Searching for lints: {}\n", filter.join(", "));
    let mut ownership_to_lints = HashMap::<_, Vec<_>>::new();

    for (path, lints) in filter_lints(lints, filter) {
        let ownership = FileOwnership::from_path(Path::new(&path), fuchsia_dir);
        ownership_to_lints.entry(ownership).or_default().push(LintFile { path, lints });
    }

    let mut created_issues = Vec::new();
    for (ownership, files) in ownership_to_lints.iter() {
        let issue = issue_template.create(monorail, ownership, files)?;
        let bug_link = format!("fxbug.dev/{}", issue.id());
        created_issues.push(issue);

        if !dryrun {
            for file in files.iter() {
                match insert_allows(&file.path, &file.lints, &bug_link) {
                    Ok(ins) => ins,
                    Err(e) => {
                        eprintln!("Failed to annotate {}: {:?}", file.path, e);
                        continue;
                    }
                };
            }
        }
    }

    if verbose {
        for (ownership, files) in ownership_to_lints.iter() {
            println!("{}:", ownership);
            for file in files.iter() {
                println!("    {}:", file.path);
                for lint in file.lints.iter() {
                    let lint_name = lint.name.strip_prefix("clippy::").unwrap_or(&lint.name);
                    println!(
                        "        {:4}:{:<3}    {}",
                        lint.span.start.line, lint.span.start.column, lint_name
                    );
                }
            }
            println!();
        }
    }

    fs::write(rollout_path, serde_json::to_string(&created_issues)?)?;

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
    fn visit_trait_item(&mut self, i: &'ast syn::TraitItem) {
        self.narrow(i.span().into());
        visit::visit_trait_item(self, i);
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

fn insert_allows(filename: &str, lints: &[Lint], bug_link: &str) -> Result<()> {
    let src = fs::read_to_string(filename)?;
    let insertions = calculate_insertions(&src, lints)?;
    fs::write(filename, apply_insertions(&src, insertions, bug_link))?;
    Ok(())
}

fn calculate_insertions(src: &str, lints: &[Lint]) -> Result<BTreeMap<usize, HashSet<String>>> {
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

fn apply_insertions(
    src: &str,
    insertions: BTreeMap<usize, HashSet<String>>,
    bug_link: &str,
) -> String {
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
            format!("{}#[allow({})] // TODO({})", indent, lints_for_line.join(", "), bug_link)
                .into(),
        );
    }
    (lines.join("\n") + if ends_with_newline { "\n" } else { "" }).to_owned()
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

    const TRAIT: &'static str = "
trait Foo {
    fn method(&self);
}";

    #[test]
    fn test_basic() {
        let name = "LINTNAME".to_owned();
        let insertions =
            calculate_insertions(BASIC, &[lint(name.clone(), (3, 5), (3, 6))]).unwrap();
        assert_eq!(
            apply_insertions(BASIC, insertions, "INSERT_LINT_BUG"),
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
            apply_insertions(BASIC, insertions, "INSERT_LINT_BUG"),
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
            apply_insertions(BASIC, insertions, "INSERT_LINT_BUG"),
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
            apply_insertions(NESTED, insertions, "INSERT_LINT_BUG").lines().nth(9).unwrap(),
            "                    #[allow(LINTNAME)] // TODO(INSERT_LINT_BUG)"
        );
    }

    #[test]
    fn test_nested_scope_block() {
        let name = "LINTNAME".to_owned();
        let insertions =
            calculate_insertions(NESTED, &[lint(name.clone(), (12, 17), (12, 18))]).unwrap();
        assert_eq!(
            apply_insertions(NESTED, insertions, "INSERT_LINT_BUG").lines().nth(8).unwrap(),
            "                #[allow(LINTNAME)] // TODO(INSERT_LINT_BUG)"
        );
    }

    #[test]
    fn test_nested_scope_multi_line() {
        let name = "LINTNAME".to_owned();
        let insertions =
            calculate_insertions(NESTED, &[lint(name.clone(), (6, 13), (7, 30))]).unwrap();
        assert_eq!(
            apply_insertions(NESTED, insertions, "INSERT_LINT_BUG").lines().nth(4).unwrap(),
            "        #[allow(LINTNAME)] // TODO(INSERT_LINT_BUG)"
        );
    }

    #[test]
    fn test_trait_narrowing() {
        let name = "LINTNAME".to_owned();
        let insertions =
            calculate_insertions(TRAIT, &[lint(name.clone(), (3, 5), (3, 6))]).unwrap();
        assert_eq!(
            apply_insertions(TRAIT, insertions, "INSERT_LINT_BUG").lines().nth(2).unwrap(),
            "    #[allow(LINTNAME)] // TODO(INSERT_LINT_BUG)"
        );
    }
}
