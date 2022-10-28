// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! doc_checker is a CLI tool to check markdown files for correctness in
//! the Fuchsia project.

use anyhow::{bail, Context, Result};
use argh::FromArgs;
use glob::glob;
use std::{fs, path::PathBuf};

pub(crate) use crate::checker::{DocCheck, DocCheckError, DocLine};
pub(crate) use crate::md_element::DocContext;
mod checker;
mod link_checker;
mod md_element;
mod parser;

/// Check the markdown documentation using a variety of checks.
#[derive(Debug, FromArgs)]
pub struct DocCheckerArgs {
    /// path to the root of the checkout of the project.
    #[argh(option, default = r#"PathBuf::from(".")"#)]
    pub root: PathBuf,

    /// name of project to check, defaults to fuchsia.
    #[argh(option, default = r#"String::from("fuchsia")"#)]
    pub project: String,

    /// (Experimental) Name of the folder inside the project
    ///  which contains documents to check. Defaults to 'docs'.
    #[argh(option, default = r#"PathBuf::from("docs")"#)]
    pub docs_folder: PathBuf,

    /// do not resolve http(s) links
    #[argh(switch)]
    pub local_links_only: bool,
}

fn main() -> Result<()> {
    let mut opt: DocCheckerArgs = argh::from_env();

    // Canonicalize the root directory so the rest of the code can rely on
    // the root directory existing and being a normalized path.
    opt.root = opt
        .root
        .canonicalize()
        .context(format!("invalid root dir for source: {:?} ", &opt.root))?;

    if let Some(errors) = do_main(opt)? {
        // Output the result
        for e in &errors {
            println!("{}: {}", e.doc_line, e.message);
        }
        bail!("{} errors found", errors.len())
    }
    Ok(())
}

/// The actual main function. It is refactored like this to make it easier
/// to run it in a unit test.
fn do_main(opt: DocCheckerArgs) -> Result<Option<Vec<DocCheckError>>> {
    let root_dir = &opt.root;
    let docs_project = &opt.project;
    let docs_dir = root_dir.join(&opt.docs_folder);

    eprintln!("Checking Project {} {:?}.", docs_project, docs_dir);

    // Find all the markdown in the docs folder.
    let pattern = format!("{}/**/*.md", docs_dir.to_string_lossy());
    let markdown_files: Vec<PathBuf> = glob(&pattern)?
        // Keep only non-error results, mapping to Option<PathBuf>
        .filter_map(|p| p.ok())
        // Keep paths with file names, mapped to str&
        // and rop the hidden files that macs sometime make.
        .filter_map(|p| {
            if let Some(name) = p.file_name()?.to_str() {
                if !name.starts_with("._") {
                    Some(p)
                } else {
                    None
                }
            } else {
                None
            }
        })
        .collect();

    // Find all the .yaml files.
    let yaml_pattern = format!("{}/**/*.yaml", docs_dir.to_string_lossy());
    let yaml_files: Vec<_> = glob(&yaml_pattern)?.collect();

    eprintln!(
        "Checking {} markdown files and {} yaml files",
        markdown_files.len(),
        yaml_files.len()
    );

    /*
    Doc checking is broken into a couple major phases.

    1. Checks are registered from the modules that have structs that implement the DocCheck trait.
    2. Each markdown file is parsed into a stream of Elements. Each element is passed to each registered checker.
    3. After all the markdown files are parsed, the post-check check is called on each checker. This allows
       checkers to perform cross-file checks and checks that used data collected from the individual documents.
    4. Each yaml file is checked for each yaml checker registered.
    5. After all the yaml is checked, the post-check check is called.
    6. All the errors are collected and returned.
    */
    let mut markdown_checks: Vec<Box<dyn DocCheck>> = vec![];
    let mut errors: Vec<DocCheckError> = vec![];

    let checks = link_checker::register_markdown_checks(&opt)?;
    for c in checks {
        markdown_checks.push(c);
    }

    let markdown_errors: Vec<DocCheckError> =
        check_markdown(&markdown_files, &mut markdown_checks)?;
    for e in markdown_errors {
        errors.push(e);
    }

    let result = if errors.is_empty() { None } else { Some(errors) };
    Ok(result)
}

/// Given the list of markdown files to check, iterate over each check, collecting any errors.
pub fn check_markdown<'a>(
    files: &[PathBuf],
    checks: &'a mut [Box<dyn DocCheck + 'static>],
) -> Result<Vec<DocCheckError>> {
    let mut errors: Vec<DocCheckError> = vec![];

    for mdfile in files {
        let mdcontent = fs::read_to_string(mdfile).expect("Unable to read file");
        let doc_context = DocContext::new(mdfile.clone(), &mdcontent);

        for element in doc_context {
            for c in &mut *checks {
                match c.check(&element) {
                    Ok(Some(check_errors)) => {
                        for e in check_errors {
                            errors.push(e);
                        }
                    }
                    Ok(None) => {}
                    Err(e) => errors.push(DocCheckError {
                        doc_line: element.doc_line(),
                        message: format!("Error {} running check: {:?} ", e, c.name()),
                    }),
                }
            }
        }
    }
    Ok(errors)
}
